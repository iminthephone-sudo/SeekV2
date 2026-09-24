"""Resume ↔ job keyword optimizer.

Scores how well a profile covers what a posting asks for, then proposes honest
changes: surface skills the resume already demonstrates but doesn't list,
reorder skills and bullets so the most relevant come first, and flag the
requirements that are genuinely missing. Nothing is added unless the
participant confirms they really have that skill (the shell uses checkboxes).
"""

from __future__ import annotations

import copy
from typing import Any

from ..storage import Store, utc_now
from .nlp import NLP
from .profiles import ProfileEngine, profile_text

TOP_KEYWORDS = 25


def _weight(kw: dict[str, Any]) -> float:
    return kw["score"] * (1.5 if kw.get("required") else 1.0)


def match(profile: dict[str, Any], job: dict[str, Any], nlp: NLP) -> dict[str, Any]:
    keywords = job.get("keywords", [])[:TOP_KEYWORDS]
    full_text = profile_text(profile)
    prof = nlp.profile_text(full_text)
    listed_skills = {nlp.key(s) for s in profile.get("skills", [])}
    listed_skills |= {nlp.key(x.get("name", "")) for x in profile.get("certifications", []) + profile.get("training", [])}

    matched, missing, demonstrated = [], [], []
    total = got = 0.0
    req_total = req_got = 0
    for kw in keywords:
        w = _weight(kw)
        total += w
        covered = nlp.covers(prof, kw["key"])
        if kw.get("required"):
            req_total += 1
            req_got += covered
        if covered:
            got += w
            entry = {**kw, "listed": kw["key"] in listed_skills}
            matched.append(entry)
            # Mentioned in experience but not in the skills list: an easy, honest win.
            if kw["kind"] == "skill" and kw["key"] not in listed_skills:
                demonstrated.append(kw["term"])
        else:
            missing.append({**kw, "advice": _advice(kw)})

    score = round(100 * got / total) if total else 0
    title = job.get("title", "")
    headline = profile.get("headline", "")
    title_match = bool(title) and nlp.key(title) in nlp.key(headline + " " + " ".join(profile.get("target_roles", [])))

    return {
        "score": score,
        "grade": _grade(score),
        "required_covered": req_got,
        "required_total": req_total,
        "matched": matched,
        "missing": missing,
        "add_to_skills": demonstrated,
        "confirm_skills": [m["term"] for m in missing if m["kind"] == "skill"],
        "bullet_ranking": rank_bullets(profile, job, nlp),
        "title_match": title_match,
        "suggested_headline": _suggest_headline(profile, job),
        "tips": _tips(score, missing, demonstrated, title_match, job),
    }


def _grade(score: int) -> str:
    if score >= 75:
        return "Strong match"
    if score >= 50:
        return "Good match"
    if score >= 30:
        return "Partial match"
    return "Stretch"


def _advice(kw: dict[str, Any]) -> str:
    if kw["kind"] == "skill":
        return (f"If they have '{kw['term']}' experience (at work, in a program, or informally), add it to Skills "
                "and mention it in a bullet. If not, it's a training target.")
    return f"If it applies, work '{kw['term']}' into a bullet or the summary using the posting's wording."


def _suggest_headline(profile: dict[str, Any], job: dict[str, Any]) -> str:
    title = job.get("title", "").split(" - ")[0].split("|")[0].strip()
    skills = profile.get("skills", [])[:2]
    if not title:
        return profile.get("headline", "")
    return f"{title}" + (f" | {' & '.join(skills)}" if skills else "")


def _tips(score: int, missing: list, demonstrated: list, title_match: bool, job: dict[str, Any]) -> list[str]:
    tips = []
    if demonstrated:
        tips.append(f"{len(demonstrated)} skill(s) are already shown in the experience but not listed — add them to Skills.")
    if not title_match and job.get("title"):
        tips.append("Mirror the job title in the headline so screeners (and ATS software) see the fit instantly.")
    req_missing = [m["term"] for m in missing if m.get("required")]
    if req_missing:
        tips.append("Required items not found: " + ", ".join(req_missing[:6]) + ". Add them only if true.")
    signals = job.get("signals", {})
    if signals.get("fair_chance"):
        tips.append("This employer describes itself as fair-chance / second-chance friendly — prioritise it.")
    if signals.get("exclusions"):
        tips.append("The posting contains exclusion language (" + "; ".join(signals["exclusions"]) +
                    "). Check local fair-chance rules with your outreach team before applying.")
    if score < 30:
        tips.append("This is a stretch role. Consider it as a goal and look at the missing skills as a training plan.")
    return tips


def rank_bullets(profile: dict[str, Any], job: dict[str, Any], nlp: NLP) -> list[dict[str, Any]]:
    """Bullets ordered by how many of the job's keywords they touch."""
    keys = [kw["key"] for kw in job.get("keywords", [])[:TOP_KEYWORDS]]
    ranked = []
    for section in ("experience", "volunteer"):
        for entry in profile.get(section, []):
            for idx, bullet in enumerate(entry.get("bullets", [])):
                prof = nlp.profile_text(bullet)
                hits = [k for k in keys if nlp.covers(prof, k)]
                ranked.append({"entry_id": entry.get("id"), "index": idx, "text": bullet, "hits": hits,
                               "relevance": len(hits)})
    ranked.sort(key=lambda r: -r["relevance"])
    return ranked


def tailor(profile: dict[str, Any], job: dict[str, Any], nlp: NLP, add_skills: list[str] | None = None,
           headline: str | None = None, reorder: bool = True) -> dict[str, Any]:
    """Return a tailored copy of the profile (not saved)."""
    out = copy.deepcopy(profile)
    job_keys = [kw["key"] for kw in job.get("keywords", [])[:TOP_KEYWORDS]]
    skills = list(out.get("skills", []))
    existing = {nlp.key(s) for s in skills}
    for s in add_skills or []:
        if s and nlp.key(s) not in existing:
            skills.append(s)
            existing.add(nlp.key(s))
    if reorder:
        def rel(skill: str) -> int:
            k = nlp.key(skill)
            return 0 if k in job_keys else 1
        skills.sort(key=rel)  # stable: keeps the participant's order within each group
        for section in ("experience", "volunteer"):
            for entry in out.get(section, []):
                def bullet_rel(b: str) -> int:
                    prof = nlp.profile_text(b)
                    return -sum(1 for k in job_keys if nlp.covers(prof, k))
                entry["bullets"] = sorted(entry.get("bullets", []), key=bullet_rel)
    out["skills"] = skills
    if headline is not None:
        out["headline"] = headline
    title = job.get("title", "")
    if title and title not in out.get("target_roles", []):
        out["target_roles"] = [title] + out.get("target_roles", [])
    return out


class OptimizerEngine:
    def __init__(self, store: Store, nlp: NLP, profiles: ProfileEngine) -> None:
        self.store = store
        self.nlp = nlp
        self.profiles = profiles

    def _job(self, job_id: str) -> dict[str, Any]:
        job = self.store.get("jobs", job_id)
        if job is None:
            raise KeyError(f"job not found: {job_id}")
        return job

    def match(self, profile_id: str, job_id: str) -> dict[str, Any]:
        profile = self.profiles.get(profile_id)
        job = self._job(job_id)
        result = match(profile, job, self.nlp)
        self.store.append_history("optimize", f"Matched '{profile.get('name')}' to '{job.get('title')}': {result['score']}%",
                                  profile_id=profile_id, job_id=job_id, score=result["score"])
        return result

    def apply(self, profile_id: str, job_id: str, add_skills: list[str] | None = None,
              headline: str | None = None, reorder: bool = True, as_copy: bool = True) -> dict[str, Any]:
        profile = self.profiles.get(profile_id)
        job = self._job(job_id)
        tailored = tailor(profile, job, self.nlp, add_skills, headline, reorder)
        if as_copy:
            label = " ".join(filter(None, [job.get("company"), job.get("title")]))[:60] or "tailored"
            tailored.pop("id", None)
            saved = self.profiles.create(f"{profile.get('name')} → {label}", profile.get("participant", ""), tailored)
        else:
            saved = self.profiles.update(profile_id, tailored)
        saved_match = match(saved, job, self.nlp)
        self.store.append_history("optimize", f"Tailored '{profile.get('name')}' for '{job.get('title')}' "
                                  f"({saved_match['score']}%)", profile_id=saved["id"], job_id=job_id,
                                  at=utc_now())
        return {"profile": saved, "match": saved_match}
