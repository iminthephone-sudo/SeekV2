"""Cover letter writer.

Letters are assembled from the participant's own material: the bullets that
best match the posting (ranked with spaCy), the skills the posting asks for
that the profile really shows, and their certifications/training. Nothing is
invented — if the profile is thin, the letter is shorter, and staff can edit
the draft freely before saving.
"""

from __future__ import annotations

import re
from datetime import date
from pathlib import Path
from typing import Any, Callable

from ..storage import NotFound, Store, as_text, new_id, utc_now
from .nlp import NLP
from .optimizer import match as match_profile
from .profiles import ProfileEngine

COLLECTION = "letters"
TONES = ("professional", "warm", "direct")

OPENINGS = {
    "professional": "I am writing to apply for the {title} position{at_company}. {hook}",
    "warm": "I was glad to come across the {title} opening{at_company}, and I'd love to be considered. {hook}",
    "direct": "I'd like to apply for the {title} role{at_company}. {hook}",
}
CLOSINGS = {
    "professional": "Thank you for considering my application. I would welcome the chance to discuss how I can "
                    "contribute{to_company}, and I am available to start {availability}.",
    "warm": "Thank you for taking the time to read my letter. I'd be grateful for the chance to talk about how I "
            "can contribute{to_company}. I'm available to start {availability}.",
    "direct": "I'm ready to get to work and available to start {availability}. I'd appreciate the chance to "
              "interview{with_company}.",
}
SIGN_OFFS = {"professional": "Sincerely,", "warm": "With gratitude,", "direct": "Thank you,"}


def _oxford(items: list[str]) -> str:
    items = [i for i in items if i]
    if len(items) <= 1:
        return "".join(items)
    if len(items) == 2:
        return f"{items[0]} and {items[1]}"
    return ", ".join(items[:-1]) + f", and {items[-1]}"


def _clause(bullet: str) -> str:
    """'Operated forklift…' -> 'operated forklift…' (keeps acronyms like 'OSHA' intact)."""
    text = bullet.strip().rstrip(".;")
    first = text.split(" ", 1)[0]
    if first and not (first.isupper() and len(first) > 1):
        text = first[0].lower() + text[1:]
    return text


def _skill_phrase(term: str) -> str:
    return term if any(c.isupper() for c in term[1:]) or term.isupper() else term.lower()


def compose(profile: dict[str, Any], job: dict[str, Any] | None, nlp: NLP, tone: str = "professional",
            hiring_manager: str = "", availability: str = "right away", fair_chance_line: bool | None = None,
            personal_note: str = "", rewrite: Callable[[str], str] | None = None) -> dict[str, Any]:
    """``rewrite``: turns a resume bullet into a clean past-tense action ("Responsible for scanning" -> "Scanned")."""
    tone = tone if tone in TONES else "professional"
    job = job or {}
    c = profile.get("contact", {})
    name = c.get("full_name") or profile.get("name", "")
    title = job.get("title") or (profile.get("target_roles") or ["open"])[0]
    company = job.get("company", "")
    at_company = f" at {company}" if company else ""
    to_company = f" to {company}" if company else " to your team"

    result = match_profile(profile, job, nlp) if job.get("keywords") else None
    matched_skills = _distinct([m["term"] for m in (result or {}).get("matched", []) if m["kind"] == "skill"])[:4]
    if not matched_skills:
        matched_skills = _distinct(profile.get("skills", []))[:3]

    # Hook: strongest credentials first.
    creds = [x.get("name", "") for x in profile.get("certifications", [])][:3]
    hook_bits = []
    hands_on = [s for s in matched_skills if not re.search(r"certif|licen[sc]e|card\b", s, re.I)]
    if hands_on:
        hook_bits.append(f"I bring hands-on experience with {_oxford([_skill_phrase(s) for s in hands_on[:3]])}")
    if creds:
        hook_bits.append(f"hold {_oxford(creds)}" if hook_bits else f"I hold {_oxford(creds)}")
    hook = (" and ".join(hook_bits) + ".") if hook_bits else "I am a dependable, hard-working team member who learns quickly."
    paragraphs = [OPENINGS[tone].format(title=title, at_company=at_company, hook=hook)]

    # Evidence: the two or three most relevant bullets, as prose.
    bullets = (result or {}).get("bullet_ranking") or []
    if not bullets:
        bullets = [{"entry_id": e.get("id"), "text": b, "relevance": 0}
                   for e in profile.get("experience", []) for b in e.get("bullets", [])[:1]]
    entries = {e.get("id"): e for e in profile.get("experience", []) + profile.get("volunteer", [])}
    evidence, used_entries = [], set()
    followups = iter(["I also", "In addition, I", "I have also"])
    for b in bullets:
        entry = entries.get(b.get("entry_id"))
        if not entry or len(evidence) >= 3:
            continue
        text = b.get("text", "")
        if "[#]" in text:
            continue  # a writing-help prompt that was never filled in doesn't belong in a letter
        if rewrite:
            text = rewrite(text) or text
        if not nlp.review_bullet(text)["starts_with_verb"]:
            continue  # "I responsible for…": only bullets that start with an action read as a sentence
        b = {**b, "text": text}
        org = entry.get("employer") or entry.get("organization") or ""
        role = entry.get("title") or entry.get("role") or ""
        if b.get("entry_id") in used_entries:
            evidence.append(f"{next(followups, 'I also')} {_clause(b['text'])}.")
        elif org:
            evidence.append(f"As {_article(role)} {role} with {org}, I {_clause(b['text'])}." if role
                            else f"At {org}, I {_clause(b['text'])}.")
        else:
            evidence.append(f"In my role as {role}, I {_clause(b['text'])}." if role else f"I {_clause(b['text'])}.")
        used_entries.add(b.get("entry_id"))
    if evidence:
        paragraphs.append(" ".join(evidence))

    # Fit: what they asked for, what the participant has done to prepare.
    fit = []
    required = _distinct([m["term"] for m in (result or {}).get("matched", []) if m.get("required")])[:3]
    if required:
        area = "that is an area" if len(required) == 1 else "those are areas"
        fit.append(f"Your posting calls for {_oxford([_skill_phrase(r) for r in required])}, and {area} where "
                   "I already have real experience.")
    training = [_with_article(t.get("name", "")) for t in profile.get("training", []) if t.get("name")][:2]
    if training:
        fit.append(f"I have also completed {_oxford(training)}, which prepared me to keep learning on the job.")
    soft = [s for s in profile.get("skills", []) if nlp.category_of(s) == "Soft Skills"][:2]
    if soft:
        fit.append(f"People I've worked with would describe me as someone who brings {_oxford([s.lower() for s in soft])} to every shift.")
    if fit:
        paragraphs.append(" ".join(fit))

    use_fc = job.get("signals", {}).get("fair_chance", False) if fair_chance_line is None else fair_chance_line
    if use_fc:
        who = company or "your organization"
        growth = ""  # training was already named above; don't repeat it
        paragraphs.append(f"I appreciate that {who} believes in giving people a fair chance. I have put steady work "
                          f"into my growth{growth}, and I'm ready to bring that same commitment to your team.")
    if personal_note.strip():
        paragraphs.append(personal_note.strip())
    with_company = f" with {company}" if company else ""
    paragraphs.append(CLOSINGS[tone].format(to_company=to_company, with_company=with_company,
                                            availability=availability or "right away"))

    header = [name]
    contact = [x for x in [", ".join(filter(None, [c.get("city"), c.get("state")])), c.get("phone"), c.get("email")] if x]
    header += contact
    header += ["", date.today().strftime("%B %d, %Y").replace(" 0", " ")]
    if company:
        header += ["", f"{hiring_manager}" if hiring_manager else "Hiring Team", company]
        if job.get("location"):
            header.append(job["location"])
    greeting = f"Dear {hiring_manager}," if hiring_manager else "Dear Hiring Manager,"
    text = "\n".join(header) + "\n\n" + greeting + "\n\n" + "\n\n".join(paragraphs) + \
        f"\n\n{SIGN_OFFS[tone]}\n{name}\n"
    return {
        "text": text, "tone": tone, "word_count": len(text.split()), "used_skills": matched_skills,
        "evidence_count": len(evidence), "fair_chance_line": bool(use_fc),
        "match_score": (result or {}).get("score"),
    }


def _distinct(terms: list[str]) -> list[str]:
    """Drop near-duplicates: 'forklift' is redundant next to 'forklift certification'."""
    lowered = [t.lower() for t in terms]
    out = []
    for i, term in enumerate(terms):
        low = lowered[i]
        if low in (o.lower() for o in out):
            continue
        if any(j != i and low != other and low in other.split(" ") + [other] and len(other) > len(low)
               for j, other in enumerate(lowered)):
            continue
        out.append(term)
    return out


def _with_article(name: str) -> str:
    words = name.split()
    if not words or words[0].lower() in {"a", "an", "the"} or name.isupper():
        return name
    if words[-1].lower() in {"program", "programme", "course", "class", "training", "apprenticeship", "academy"}:
        return f"the {name}"
    return name


def _article(word: str) -> str:
    return "an" if word[:1].lower() in "aeiou" else "a"


class CoverLetterEngine:
    def __init__(self, store: Store, nlp: NLP, profiles: ProfileEngine,
                 rewrite: Callable[[str], str] | None = None) -> None:
        self.store = store
        self.nlp = nlp
        self.profiles = profiles
        self.rewrite = rewrite

    def generate(self, profile_id: str, job_id: str = "", save: bool = True, **options: Any) -> dict[str, Any]:
        profile = self.profiles.get(profile_id)
        job = self.store.get("jobs", job_id) if job_id else None
        if job_id and job is None:
            raise NotFound(f"job not found: {job_id}")
        allowed = {"tone", "hiring_manager", "availability", "fair_chance_line", "personal_note"}
        result = compose(profile, job, self.nlp, rewrite=self.rewrite,
                         **{k: v for k, v in options.items() if k in allowed})
        if save:
            letter = {
                "id": new_id("ltr"), "profile_id": profile_id, "job_id": job_id or "",
                "title": f"{profile.get('name')} → {(job or {}).get('company') or (job or {}).get('title') or 'General'}",
                "text": result["text"], "tone": result["tone"], "created_at": utc_now(), "updated_at": utc_now(),
            }
            self.store.put(COLLECTION, letter["id"], letter)
            self.store.append_history("letter", f"Drafted cover letter '{letter['title']}'", letter_id=letter["id"],
                                      profile_id=profile_id, job_id=job_id)
            result["letter"] = letter
        return result

    def save(self, letter_id: str = "", text: str = "", title: str = "", profile_id: str = "",
             job_id: str = "") -> dict[str, Any]:
        """Save a letter's text. Without ``letter_id`` a new letter is created in this one call (a hand-written
        letter must never depend on a second request arriving)."""
        if not letter_id:
            profile = self.profiles.get(profile_id)
            job = self.store.get("jobs", job_id) if job_id else None
            letter = {"id": new_id("ltr"), "profile_id": profile_id, "job_id": job_id or "",
                      "title": title or f"{profile.get('name')} → {(job or {}).get('company') or (job or {}).get('title') or 'General'}",
                      "text": as_text(text), "tone": "", "created_at": utc_now(), "updated_at": utc_now()}
            self.store.put(COLLECTION, letter["id"], letter)
            self.store.append_history("letter", f"Wrote cover letter '{letter['title']}'", letter_id=letter["id"],
                                      profile_id=profile_id, job_id=job_id)
            return letter
        letter = self.get(letter_id)
        letter["text"] = as_text(text)
        if title:
            letter["title"] = title
        letter["updated_at"] = utc_now()
        return self.store.put(COLLECTION, letter_id, letter)

    def get(self, letter_id: str) -> dict[str, Any]:
        letter = self.store.get(COLLECTION, letter_id)
        if letter is None:
            raise NotFound(f"letter not found: {letter_id}")
        return letter

    def list(self, profile_id: str = "") -> list[dict[str, Any]]:
        rows = [{k: l.get(k, "") for k in ("id", "title", "profile_id", "job_id", "tone", "updated_at")}
                for l in self.store.all(COLLECTION) if not profile_id or l.get("profile_id") == profile_id]
        rows.sort(key=lambda r: r["updated_at"], reverse=True)
        return rows

    def delete(self, letter_id: str) -> dict[str, Any]:
        self.get(letter_id)
        self.store.delete(COLLECTION, letter_id)
        return {"deleted": letter_id}

    def export(self, letter_id: str, format: str, path: str) -> str:  # noqa: A002 - contract name
        letter = self.get(letter_id)
        out = Path(path)
        out.parent.mkdir(parents=True, exist_ok=True)
        fmt = format.lower().lstrip(".")
        if fmt == "docx":
            try:
                from docx import Document
                from docx.shared import Pt
            except ImportError as exc:  # pragma: no cover
                raise RuntimeError("DOCX export needs python-docx: pip install python-docx") from exc
            doc = Document()
            doc.styles["Normal"].font.size = Pt(11)
            for block in re.split(r"\n{2,}", letter["text"]):
                para = doc.add_paragraph()
                lines = block.split("\n")
                for i, line in enumerate(lines):
                    run = para.add_run(line)
                    if i < len(lines) - 1:
                        run.add_break()
            doc.save(str(out))
        elif fmt in ("txt", "text", "md"):
            out.write_text(letter["text"], encoding="utf-8")
        else:
            raise ValueError(f"unsupported format: {fmt} (PDF is printed by the shell)")
        return str(out)
