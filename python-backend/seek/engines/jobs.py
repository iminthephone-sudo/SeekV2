"""Job postings: pull from a URL (or pasted text), parse, extract keywords, save.

Most job boards embed a schema.org ``JobPosting`` block (JSON-LD) for search
engines; that is read first because it is clean and structured. Otherwise the
page's main text is extracted heuristically. Some large boards block automated
requests — the engine detects that and tells staff to paste the description,
which goes through exactly the same pipeline.
"""

from __future__ import annotations

import html as html_lib
import json
import re
from typing import Any, Callable
from urllib.parse import urlparse

from ..storage import Store, new_id, utc_now
from .nlp import NLP, stem

COLLECTION = "jobs"
STATUSES = ["saved", "applying", "applied", "interview", "offer", "hired", "closed"]
MAX_BYTES = 4 * 1024 * 1024
# Bot filters (Cloudflare, Akamai, DataDome) reject anything that doesn't look like a real browser:
# an extra product token such as "SEEK/2.0", a non-canonical "Chrome/126.0" version, or a header set
# missing what Chrome always sends. Keep this matching a current Chrome release exactly.
USER_AGENT = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
              "Chrome/131.0.0.0 Safari/537.36")
BROWSER_HEADERS = {
    "User-Agent": USER_AGENT,
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "Accept-Language": "en-US,en;q=0.9",
    "Sec-Ch-Ua": '"Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24"',
    "Sec-Ch-Ua-Mobile": "?0",
    "Sec-Ch-Ua-Platform": '"Windows"',
    "Sec-Fetch-Dest": "document",
    "Sec-Fetch-Mode": "navigate",
    "Sec-Fetch-Site": "none",
    "Sec-Fetch-User": "?1",
    "Upgrade-Insecure-Requests": "1",
}
BLOCKED_STATUSES = (401, 403, 429, 999)

SECTION_HEADINGS = {
    "responsibilities": ("responsibilities", "duties", "what you'll do", "what you will do", "the role",
                         "job duties", "essential functions", "day to day", "your role"),
    "requirements": ("requirements", "qualifications", "what you need", "what we're looking for",
                     "what we are looking for", "who you are", "must have", "skills", "minimum qualifications",
                     "required", "preferred qualifications", "nice to have"),
    "benefits": ("benefits", "perks", "what we offer", "compensation", "pay"),
}

SIGNALS = {
    "fair_chance": r"fair[- ]chance|second[- ]chance|justice[- ]involved|re-?entry|returning citizens?|"
                   r"criminal (history|record)s? (will be )?considered|ban the box|record[- ]friendly",
    "background_check": r"background (check|screen|investigation)",
    "drug_screen": r"drug (test|screen)|drug[- ]free workplace",
    "driving_record": r"(clean|good|acceptable) (driving|motor vehicle) record|\bMVR\b",
    "license_required": r"valid driver'?s licen[sc]e",
}
# Hiring-policy words describe the employer, not a skill to put on a resume.
POLICY_TERMS = (r"\b(criminal|record|records|conviction|felony|applicants?|employer|equal opportunity|eeo|"
                r"background|drug|e-verify|accommodation|disabilit(y|ies)|veteran|benefits?|401k|pto)\b")
EXCLUSION_PATTERNS = r"no (felon(y|ies)|convictions?|criminal record)|must not have (a|any) (felony|conviction)"


class FetchError(RuntimeError):
    """Readable error shown to staff as-is."""


def _text_from_html(fragment: str) -> str:
    try:
        from bs4 import BeautifulSoup
    except ImportError:
        text = re.sub(r"<(br|/p|/li|/h\d|/div)[^>]*>", "\n", fragment, flags=re.I)
        text = re.sub(r"<li[^>]*>", "\n• ", text, flags=re.I)
        return _tidy(html_lib.unescape(re.sub(r"<[^>]+>", " ", text)))
    soup = BeautifulSoup(fragment, "html.parser")
    for li in soup.find_all("li"):
        li.insert_before("\n• ")
    for tag in soup.find_all(["br", "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "tr"]):
        tag.insert_after("\n")
    return _tidy(soup.get_text(" "))


def _tidy(text: str) -> str:
    lines = [re.sub(r"[ \t ]+", " ", ln).strip() for ln in text.splitlines()]
    out, blank = [], False
    for ln in lines:
        if not ln:
            if not blank and out:
                out.append("")
            blank = True
            continue
        out.append(ln)
        blank = False
    return "\n".join(out).strip()


def _first(value: Any) -> Any:
    return value[0] if isinstance(value, list) and value else value


def _find_jobposting(node: Any) -> dict | None:
    if isinstance(node, list):
        for item in node:
            found = _find_jobposting(item)
            if found:
                return found
    elif isinstance(node, dict):
        types = node.get("@type")
        types = types if isinstance(types, list) else [types]
        if "JobPosting" in types:
            return node
        for key in ("@graph", "mainEntity", "itemListElement"):
            if key in node:
                found = _find_jobposting(node[key])
                if found:
                    return found
    return None


def _location(jp: dict) -> str:
    loc = _first(jp.get("jobLocation"))
    if isinstance(loc, dict):
        addr = loc.get("address") or {}
        if isinstance(addr, dict):
            return ", ".join(filter(None, [addr.get("addressLocality"), addr.get("addressRegion")]))
        return str(addr)
    if jp.get("jobLocationType") == "TELECOMMUTE":
        return "Remote"
    return ""


def _salary(jp: dict) -> str:
    base = jp.get("baseSalary")
    if not isinstance(base, dict):
        return str(base or "")
    value = base.get("value") or {}
    currency = base.get("currency", "")
    if isinstance(value, dict):
        lo, hi, unit = value.get("minValue"), value.get("maxValue"), value.get("unitText", "")
        amount = f"{lo}–{hi}" if lo and hi and lo != hi else str(lo or hi or value.get("value") or "")
        return " ".join(filter(None, [currency, amount, f"per {unit.lower()}" if unit else ""])).strip()
    return f"{currency} {value}".strip()


def parse_html(page: str, url: str = "") -> dict[str, Any]:
    """Turn a job page's HTML into a raw posting dict (no NLP yet)."""
    for match in re.finditer(r"<script[^>]+application/ld\+json[^>]*>(.*?)</script>", page, re.S | re.I):
        raw = match.group(1).strip()
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            try:
                data = json.loads(html_lib.unescape(raw))
            except json.JSONDecodeError:
                continue
        jp = _find_jobposting(data)
        if jp:
            org = jp.get("hiringOrganization")
            company = org.get("name", "") if isinstance(org, dict) else str(org or "")
            emp_type = jp.get("employmentType", "")
            return {
                "title": html_lib.unescape(str(jp.get("title", ""))).strip(),
                "company": html_lib.unescape(company).strip(),
                "location": _location(jp),
                "employment_type": ", ".join(emp_type) if isinstance(emp_type, list) else str(emp_type),
                "salary": _salary(jp),
                "date_posted": str(jp.get("datePosted", "")),
                "description": _text_from_html(str(jp.get("description", ""))),
                "source": "json-ld",
            }

    try:
        from bs4 import BeautifulSoup
    except ImportError:
        title = re.search(r"<title[^>]*>(.*?)</title>", page, re.S | re.I)
        return {"title": _tidy(html_lib.unescape(title.group(1))) if title else "", "company": "",
                "location": "", "employment_type": "", "salary": "", "date_posted": "",
                "description": _text_from_html(page), "source": "html"}

    soup = BeautifulSoup(page, "html.parser")

    def meta(*names: str) -> str:
        for n in names:
            tag = soup.find("meta", attrs={"property": n}) or soup.find("meta", attrs={"name": n})
            if tag and tag.get("content"):
                return tag["content"].strip()
        return ""

    title = meta("og:title", "twitter:title") or (soup.title.get_text(strip=True) if soup.title else "")
    h1 = soup.find("h1")
    if h1 and h1.get_text(strip=True):
        title = h1.get_text(" ", strip=True)
    company = meta("og:site_name")
    for tag in soup(["script", "style", "noscript", "nav", "header", "footer", "form", "svg", "iframe"]):
        tag.decompose()
    candidates = soup.find_all(["main", "article", "section", "div"])
    best, best_len = soup.body or soup, 0
    for cand in candidates:
        text = cand.get_text(" ", strip=True)
        # Prefer dense blocks with list items: that is what job descriptions look like.
        score = len(text) + 200 * len(cand.find_all("li", recursive=True))
        link_text = sum(len(a.get_text(strip=True)) for a in cand.find_all("a"))
        if link_text > 0.5 * max(len(text), 1):
            continue
        if score > best_len and len(text) < 40000:
            best, best_len = cand, score
    return {
        "title": title, "company": company, "location": "", "employment_type": "", "salary": "",
        "date_posted": "", "description": _text_from_html(str(best)), "source": "html",
    }


def _has_curl_cffi() -> bool:
    try:
        import curl_cffi  # noqa: F401
    except ImportError:
        return False
    return True


def _open(url: str, impersonate: bool) -> Any:
    """Start a streaming GET for ``url``; the caller closes the response."""
    if impersonate:
        from curl_cffi import requests as curl_requests
        # Most job-board 403s aren't about headers at all: the WAF fingerprints the TLS/HTTP2
        # handshake, and Python's ssl module is on every blocklist. curl_cffi replays Chrome's
        # handshake (and sends Chrome's own headers), so the request is indistinguishable from a browser.
        return curl_requests.get(url, impersonate="chrome", timeout=20, stream=True, allow_redirects=True)
    import requests
    return requests.get(url, headers=BROWSER_HEADERS, timeout=20, stream=True, allow_redirects=True)


def _decode(body: bytes, content_type: str) -> str:
    # Don't trust requests' resp.encoding: it assumes ISO-8859-1 for text/html without a charset,
    # which turns UTF-8 dashes, bullets and apostrophes into mojibake. Header, then <meta>, then UTF-8.
    match = (re.search(r"charset=[\"']?([\w-]+)", content_type or "", re.I)
             or re.search(r"<meta[^>]+charset=[\"']?([\w-]+)", body[:4096].decode("ascii", "ignore"), re.I))
    try:
        return body.decode(match.group(1) if match else "utf-8", errors="replace")
    except LookupError:
        return body.decode("utf-8", errors="replace")


def fetch_url(url: str, progress: Callable[[int, str], None] | None = None) -> tuple[str, str]:
    url = url.strip()
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise FetchError("Enter a full web address starting with http:// or https://")
    try:
        import requests  # noqa: F401
    except ImportError as exc:  # pragma: no cover
        raise FetchError("URL import needs the 'requests' package: pip install requests") from exc
    # Browser-grade client first; plain requests is the fallback if curl_cffi is missing or fails.
    attempts = [True, False] if _has_curl_cffi() else [False]
    if progress:
        progress(15, f"Contacting {parsed.netloc}…")
    resp, failure = None, None
    for impersonate in attempts:
        try:
            candidate = _open(url, impersonate)
        except OSError as exc:  # requests' and curl_cffi's exceptions both derive from OSError
            failure = exc
            continue
        if resp is not None:
            resp.close()
        resp = candidate
        if resp.status_code not in BLOCKED_STATUSES:
            break
    if resp is None:
        raise FetchError(f"Couldn't reach {parsed.netloc}: {failure.__class__.__name__}. Check the internet "
                         "connection, or paste the job description instead.") from failure
    try:
        if resp.status_code in BLOCKED_STATUSES:
            hint = "" if len(attempts) > 1 else (" (Installing the 'curl_cffi' package lets SEEK read more "
                                                 "job boards: pip install curl_cffi)")
            raise FetchError(f"{parsed.netloc} blocked automatic reading (HTTP {resp.status_code}). Open the "
                             "posting in a browser, copy the description, and use 'Paste description'." + hint)
        if resp.status_code >= 400:
            raise FetchError(f"{parsed.netloc} returned HTTP {resp.status_code}. Check the link.")
        if progress:
            progress(40, "Downloading posting…")
        chunks, size = [], 0
        for chunk in resp.iter_content(None):  # None = as received; curl_cffi ignores sizes anyway
            size += len(chunk)
            if size > MAX_BYTES:
                raise FetchError("That page is unusually large; paste the description instead.")
            chunks.append(chunk)
        page = _decode(b"".join(chunks), resp.headers.get("Content-Type", ""))
        return page, str(resp.url)
    finally:
        resp.close()


def split_sections(description: str) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {k: [] for k in SECTION_HEADINGS}
    current = None
    for line in description.splitlines():
        clean = line.strip().strip(":").strip()
        if not clean:
            continue
        lowered = clean.lower()
        heading = None
        if len(clean) < 60 and not clean.startswith("•"):
            for name, cues in SECTION_HEADINGS.items():
                if any(lowered.startswith(c) or lowered == c for c in cues):
                    heading = name
                    break
        if heading:
            current = heading
            continue
        if current and (clean.startswith("•") or len(clean) < 220):
            sections[current].append(clean.lstrip("•- ").strip())
    return sections


def clean_title(title: str, company: str = "") -> str:
    title = re.sub(r"\s+", " ", title or "").strip()
    for sep in (" — ", " – ", " | ", " - ", " at "):
        head, found, tail = title.partition(sep)
        if found and head.strip() and (not company or company.lower() in tail.lower() or sep != " at "):
            title = head.strip()
            break
    return title


def detect_signals(text: str) -> dict[str, Any]:
    found = {name: bool(re.search(pattern, text, re.I)) for name, pattern in SIGNALS.items()}
    found["exclusions"] = [m.group(0) for m in re.finditer(EXCLUSION_PATTERNS, text, re.I)]
    return found


class JobEngine:
    def __init__(self, store: Store, nlp: NLP) -> None:
        self.store = store
        self.nlp = nlp

    def _build(self, raw: dict[str, Any], url: str = "") -> dict[str, Any]:
        text = raw.get("description", "")
        if len(text.split()) < 25:
            raise FetchError("Couldn't find a job description on that page. Paste the description instead.")
        title = clean_title(raw.get("title", ""), raw.get("company", ""))
        keywords = self._keywords(text, raw.get("company", ""), title)
        job = {
            "id": new_id("job"),
            "url": url,
            "title": title or "Untitled posting",
            "company": raw.get("company", ""),
            "location": raw.get("location", ""),
            "employment_type": raw.get("employment_type", ""),
            "salary": raw.get("salary", ""),
            "date_posted": raw.get("date_posted", ""),
            "description": text,
            "sections": split_sections(text),
            "keywords": keywords,
            "signals": detect_signals(text),
            "source": raw.get("source", "paste"),
            "status": "saved",
            "notes": "",
            "fetched_at": utc_now(),
            "updated_at": utc_now(),
        }
        self.store.put(COLLECTION, job["id"], job)
        where = f" at {job['company']}" if job["company"] else ""
        self.store.append_history("job", f"Saved posting '{job['title']}'{where}", job_id=job["id"], url=url)
        return job

    def _keywords(self, text: str, company: str, title: str = "") -> list[dict[str, Any]]:
        """Posting keywords minus the employer's name, the bare job title and hiring-policy language.

        The title is matched separately (headline suggestion), so "Associate" or
        "Warehouse Associate" as keywords would only add noise.
        """
        def words_of(s: str) -> set[str]:
            return {stem(w.lower()) for w in re.findall(r"[A-Za-z][\w&'-]+", s or "")}

        company_words = words_of(company) | {"inc", "llc", "co", "company", "corporation"}
        title_words = words_of(title)
        out = []
        for kw in self.nlp.keywords(text, top=45):
            words = {stem(w) for w in kw.key.split()}
            if kw.kind != "skill" and words and (words <= company_words or words <= title_words
                                                 or words <= company_words | title_words):
                continue
            if any(re.search(p, kw.term, re.I) for p in SIGNALS.values()) or re.search(POLICY_TERMS, kw.term, re.I):
                continue
            out.append(kw.to_dict())
        return out[:40]

    def fetch(self, url: str, progress: Callable[[int, str], None] | None = None) -> dict[str, Any]:
        page, final_url = fetch_url(url, progress)
        if progress:
            progress(65, "Reading the posting…")
        raw = parse_html(page, final_url)
        if progress:
            progress(85, "Finding keywords with spaCy…")
        return self._build(raw, url=final_url)

    def from_text(self, text: str, title: str = "", company: str = "", url: str = "",
                  location: str = "") -> dict[str, Any]:
        text = _tidy(text or "")
        if not title:
            first = next((ln for ln in text.splitlines() if ln.strip()), "")
            title = first[:80] if len(first) < 80 else ""
        return self._build({"title": title, "company": company, "location": location, "description": text,
                            "source": "paste"}, url=url)

    def list(self) -> list[dict[str, Any]]:
        rows = [{k: j.get(k, "") for k in ("id", "title", "company", "location", "status", "url", "fetched_at",
                                           "updated_at")} | {"fair_chance": j.get("signals", {}).get("fair_chance", False)}
                for j in self.store.all(COLLECTION)]
        rows.sort(key=lambda r: r.get("updated_at", ""), reverse=True)
        return rows

    def get(self, job_id: str) -> dict[str, Any]:
        job = self.store.get(COLLECTION, job_id)
        if job is None:
            raise KeyError(f"job not found: {job_id}")
        return job

    def update(self, job_id: str, changes: dict[str, Any]) -> dict[str, Any]:
        job = self.get(job_id)
        for key in ("title", "company", "location", "notes", "salary", "employment_type"):
            if key in changes:
                job[key] = str(changes[key] or "")
        if "status" in changes:
            status = str(changes["status"])
            if status not in STATUSES:
                raise ValueError(f"status must be one of {STATUSES}")
            if status != job.get("status"):
                self.store.append_history("job", f"'{job['title']}' moved to {status}", job_id=job_id, status=status)
            job["status"] = status
        if "description" in changes and changes["description"] != job.get("description"):
            job["description"] = _tidy(changes["description"])
            job["sections"] = split_sections(job["description"])
            job["keywords"] = self._keywords(job["description"], job.get("company", ""), job.get("title", ""))
            job["signals"] = detect_signals(job["description"])
        job["updated_at"] = utc_now()
        self.store.put(COLLECTION, job_id, job)
        return job

    def delete(self, job_id: str) -> dict[str, Any]:
        job = self.get(job_id)
        self.store.delete(COLLECTION, job_id)
        self.store.append_history("job", f"Deleted posting '{job.get('title')}'", job_id=job_id)
        return {"deleted": job_id}
