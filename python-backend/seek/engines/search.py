"""Job search across LinkedIn and Indeed.

LinkedIn publishes signed-out search results as small HTML cards (the same feed
its public jobs pages use), which the engine downloads and parses directly.
Indeed answers automated requests with a Cloudflare check, so its results page
is normally loaded by the shell in its built-in browser — with the active
profile's Indeed sign-in, if any — and the rendered HTML comes back through
``search.parse``. Both paths produce the same result rows.

Results are only a list of links; importing a result goes through the normal
posting import (``job.fetch``, or the built-in browser when a site blocks it).
"""

from __future__ import annotations

import html as html_lib
import json
import re
from typing import Any, Callable
from urllib.parse import urlencode, urljoin

from ..storage import Store
from .jobs import FetchError, fetch_url, linkedin_job_id, normalize_url

SOURCES = {
    "linkedin": {"label": "LinkedIn", "per_page": 25},
    "indeed": {"label": "Indeed", "per_page": 10},
}
MAX_QUERY = 120


def search_url(source: str, query: str, location: str = "", page: int = 0, fair_chance: bool = False) -> str:
    q = query.strip()
    if fair_chance:
        q = f'{q} "fair chance"'.strip()
    if source == "linkedin":
        params = {"keywords": q, "location": location.strip(), "start": page * SOURCES["linkedin"]["per_page"]}
        return "https://www.linkedin.com/jobs-guest/jobs/api/seeMoreJobPostings/search?" + urlencode(params)
    if source == "indeed":
        params = {"q": q, "l": location.strip()}
        if page:
            params["start"] = page * SOURCES["indeed"]["per_page"]
        return "https://www.indeed.com/jobs?" + urlencode(params)
    raise ValueError(f"unknown source: {source}")


def _soup(page: str):
    from bs4 import BeautifulSoup
    return BeautifulSoup(page, "html.parser")


def _text(el) -> str:
    return re.sub(r"\s+", " ", el.get_text(" ", strip=True)).strip() if el else ""


def parse_linkedin(page: str) -> list[dict[str, Any]]:
    """Cards from LinkedIn's public search feed (or a signed-in search page)."""
    soup = _soup(page)
    out = []
    for card in soup.select("[data-entity-urn*=jobPosting], .job-search-card, .base-search-card"):
        urn = card.get("data-entity-urn", "")
        link = card.select_one("a.base-card__full-link, a[href*='/jobs/view/']")
        href = link.get("href", "") if link else ""
        job_id = (re.search(r"jobPosting:(\d+)", urn) or [None, ""])[1] or linkedin_job_id(href)
        title = _text(card.select_one(".base-search-card__title, h3")) or _text(link)
        if not job_id or not title:
            continue
        posted = card.select_one("time")
        out.append({
            "source": "linkedin", "id": f"linkedin:{job_id}", "title": title,
            "company": _text(card.select_one(".base-search-card__subtitle, h4")),
            "location": _text(card.select_one(".job-search-card__location")),
            "posted": _text(posted), "posted_date": posted.get("datetime", "") if posted else "",
            "salary": _text(card.select_one(".job-search-card__salary-info")),
            "snippet": "", "url": f"https://www.linkedin.com/jobs/view/{job_id}/",
        })
    return _dedupe(out)


def _indeed_json(page: str) -> list[dict[str, Any]]:
    """Indeed embeds its result cards as JSON (``mosaic-provider-jobcards``); read that when present."""
    m = re.search(r'window\.mosaic\.providerData\["mosaic-provider-jobcards"\]\s*=\s*', page)
    if not m:
        return []
    try:
        data, _ = json.JSONDecoder().raw_decode(page, m.end())
    except json.JSONDecodeError:
        return []
    results = (((data.get("metaData") or {}).get("mosaicProviderJobCardsModel") or {}).get("results") or [])
    out = []
    for r in results:
        jk = str(r.get("jobkey", ""))
        if not re.fullmatch(r"[0-9a-f]{16}", jk):
            continue
        snippet = re.sub(r"<[^>]+>", " ", html_lib.unescape(str(r.get("snippet", ""))))
        salary = (r.get("salarySnippet") or {}).get("text", "") if isinstance(r.get("salarySnippet"), dict) else ""
        out.append({
            "source": "indeed", "id": f"indeed:{jk}",
            "title": html_lib.unescape(str(r.get("displayTitle") or r.get("title") or "")).strip(),
            "company": html_lib.unescape(str(r.get("company", ""))).strip(),
            "location": html_lib.unescape(str(r.get("formattedLocation", ""))).strip(),
            "posted": str(r.get("formattedRelativeTime", "")), "posted_date": "",
            "salary": salary, "snippet": re.sub(r"\s+", " ", snippet).strip()[:300],
            "url": f"https://www.indeed.com/viewjob?jk={jk}",
        })
    return [r for r in out if r["title"]]


def parse_indeed(page: str) -> list[dict[str, Any]]:
    """Result cards from an Indeed search page: embedded JSON first, then the rendered cards."""
    rows = _indeed_json(page)
    if rows:
        return _dedupe(rows)
    soup = _soup(page)
    out = []
    for link in soup.select("a[data-jk]"):
        jk = link.get("data-jk", "")
        if not re.fullmatch(r"[0-9a-f]{16}", jk):
            continue
        card = link.find_parent(class_=re.compile(r"job_seen_beacon|cardOutline|result")) or link.find_parent("li") or link
        title_el = link.select_one("span[title]") or link
        out.append({
            "source": "indeed", "id": f"indeed:{jk}",
            "title": title_el.get("title") or _text(title_el),
            "company": _text(card.select_one("[data-testid=company-name], .companyName")),
            "location": _text(card.select_one("[data-testid=text-location], .companyLocation")),
            "posted": _text(card.select_one("[data-testid=myJobsStateDate], .date")).replace("Posted", "").strip(),
            "posted_date": "",
            "salary": _text(card.select_one("[data-testid=attribute_snippet_testid], .salary-snippet-container")),
            "snippet": _text(card.select_one(".job-snippet, [data-testid=jobsnippet_footer]"))[:300],
            "url": f"https://www.indeed.com/viewjob?jk={jk}",
        })
    return _dedupe([r for r in out if r["title"]])


def _dedupe(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen, out = set(), []
    for r in rows:
        if r["id"] not in seen:
            seen.add(r["id"])
            out.append(r)
    return out


PARSERS: dict[str, Callable[[str], list[dict[str, Any]]]] = {"linkedin": parse_linkedin, "indeed": parse_indeed}


class SearchEngine:
    def __init__(self, store: Store) -> None:
        self.store = store

    def _mark_saved(self, rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
        saved = {normalize_url(j.get("url", "")) for j in self.store.all("jobs") if j.get("url")}
        for r in rows:
            r["saved"] = normalize_url(r["url"]) in saved
        return rows

    def search(self, query: str, location: str = "", sources: list[str] | None = None, page: int = 0,
               fair_chance: bool = False, direct: list[str] | None = None,
               progress: Callable[[int, str], None] | None = None) -> dict[str, Any]:
        """Search each source. ``direct``: sources to download here; the rest (and any that block the
        download) come back under ``browser`` for the shell to load in its built-in browser."""
        query = re.sub(r"\s+", " ", query or "").strip()[:MAX_QUERY]
        location = re.sub(r"\s+", " ", location or "").strip()[:MAX_QUERY]
        if not query:
            raise ValueError("Type what kind of job to search for, e.g. “warehouse” or “line cook”.")
        sources = [s for s in (sources or list(SOURCES)) if s in SOURCES] or list(SOURCES)
        direct = list(SOURCES) if direct is None else [s for s in direct if s in SOURCES]
        page = max(0, min(int(page or 0), 20))
        results: list[dict[str, Any]] = []
        browser: list[dict[str, str]] = []
        errors: list[dict[str, str]] = []
        for n, source in enumerate(sources):
            url = search_url(source, query, location, page, fair_chance)
            if source not in direct:
                browser.append({"source": source, "url": url})
                continue
            if progress:
                progress(10 + 70 * n // len(sources), f"Searching {SOURCES[source]['label']}…")
            try:
                body, _ = fetch_url(url)
            except FetchError as exc:
                if exc.blocked:
                    browser.append({"source": source, "url": url})
                else:
                    errors.append({"source": source, "message": str(exc)})
                continue
            results.extend(PARSERS[source](body))
        return {"query": query, "location": location, "page": page, "results": self._mark_saved(results),
                "browser": browser, "errors": errors}

    def parse(self, source: str, html: str) -> dict[str, Any]:
        """Results from a search page the shell loaded in its browser."""
        if source not in PARSERS:
            raise ValueError(f"unknown source: {source}")
        return {"source": source, "results": self._mark_saved(PARSERS[source](html or ""))}
