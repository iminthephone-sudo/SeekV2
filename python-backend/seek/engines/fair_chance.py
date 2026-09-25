"""Fair-chance review — the part of SEEK built for justice outreach.

People coming home from incarceration often have real, marketable experience
(kitchen crews, correctional industries, vocational certificates) that is
written in a way that leads with the setting rather than the skill. This engine
points those spots out and suggests skill-first wording, and it finds
employment gaps that could be filled with training, programs or volunteer work.

It never hides or changes anything by itself and never advises misstating facts:
if an application or interviewer asks directly, the answer should be truthful.
The goal is only that a resume leads with what someone can do.
"""

from __future__ import annotations

import re
from datetime import date
from typing import Any

# term regex -> suggestion
SENSITIVE_TERMS: list[tuple[str, str]] = [
    (r"\b(department|dept\.?) of corrections?\b|\bDOC\b",
     "Name the program or industry instead (e.g. 'Correctional Industries — Furniture Shop' or the vocational "
     "program's name). It is accurate and puts the work first."),
    (r"\bcorrectional (facility|center|institution)\b|\bpenitentiary\b|\bprison\b|\bjail\b|\bdetention (center|facility)\b",
     "Resumes list the work, not the setting. Consider using the program, shop or crew name as the employer "
     "and describing the tasks and results."),
    (r"\binmates?\b|\boffenders?\b|\bprisoners?\b",
     "Describe the role by its job title ('Line Cook', 'Tutor', 'Maintenance Crew') and the people served "
     "('residents', 'peers', 'learners')."),
    (r"\bincarcerat\w*\b|\bwhile (serving|locked up|inside)\b|\bmy sentence\b",
     "The resume doesn't need to explain where you were — list the job, training or program and what you did."),
    (r"\b(felony|felonies|misdemeanou?rs?|convictions?|convicted|criminal record|(?<!cardiac )(?<!respiratory )arrests?)\b",
     "Legal history doesn't belong on a resume. Answer application questions honestly when asked; your "
     "caseworker can help you prepare a short, forward-looking explanation for interviews."),
    (r"\b(parole|probation(?!ary| period))( officer)?\b",
     "Supervision status isn't resume content. If it affects scheduling, discuss it with the employer after an "
     "offer or when asked."),
    (r"\bhalfway house\b|\bwork[- ]release\b|\breentry (center|facility)\b",
     "If this was a real job, list the employer and duties. The housing or program setting can be left off."),
]

POSITIVE_REFRAMES = {
    "kitchen": "Food Service Worker / Line Cook",
    "laundry": "Laundry Operations Worker",
    "maintenance": "Facility Maintenance Worker",
    "tutor": "Peer Tutor / Literacy Mentor",
    "library": "Library Assistant",
    "barber": "Barber (licensed program)",
    "welding": "Welder (vocational program)",
    "firefight": "Wildland Firefighter",
    "clerk": "Administrative Clerk",
    "janitor": "Custodian",
    "landscap": "Grounds Crew Member",
}

GUIDANCE = [
    {"title": "Lead with skills", "body": "Open with a short summary of what you can do and the certifications you "
     "hold. Employers skim the top third of the page first."},
    {"title": "Program work is real work", "body": "Correctional industries, kitchen crews, maintenance, tutoring and "
     "firefighting crews are real jobs. List them with a job title, dates and results."},
    {"title": "Training counts", "body": "Vocational certificates, GED/HSE, OSHA, ServSafe, forklift and college "
     "credits earned during a program belong under Certifications or Training."},
    {"title": "Gaps: fill, don't explain", "body": "Use years instead of months if that helps, and fill time with "
     "training, programs, volunteering or self-study. Save explanations for the interview."},
    {"title": "Be truthful when asked", "body": "Never misstate history on an application. When a question is asked "
     "directly, answer honestly and briefly, then move to what you've done since and why you're ready."},
    {"title": "Look for fair-chance employers", "body": "Many cities and states limit when employers may ask about "
     "records (\"ban the box\"). Check local rules with your outreach team and prioritise fair-chance employers."},
    {"title": "Bonding and tax credits", "body": "The Federal Bonding Program and the Work Opportunity Tax Credit "
     "(WOTC) can make hiring easier for employers. Your outreach team can confirm eligibility."},
    {"title": "References", "body": "Program instructors, supervisors, mentors and volunteer coordinators make strong "
     "references. Ask first and keep their contact details current."},
]

_MONTHS = {m: i for i, m in enumerate(
    ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"], start=1)}


_SEASONS = {"spring": (3, 5), "summer": (6, 8), "fall": (9, 11), "autumn": (9, 11), "winter": (1, 2)}


def _year(value: str) -> int | None:
    y = int(value)
    return y if 1900 <= y <= 2100 else None


def parse_when(text: str, *, is_end: bool = False) -> date | None:
    """Parse the loose dates people type: '2019', 'Jan 2019', '01/2019', '2019-05', 'Summer 2020',
    '2019-2021' (a range: the start or end year, depending on ``is_end``), 'Present'."""
    t = (text or "").strip().lower()
    if not t:
        return None
    if re.match(r"(present|current|now|today|ongoing)\b", t):  # also "Present (part-time)"
        return date.today()
    m = re.search(r"\b(\d{4})\s*[-–—/]\s*(\d{4})\b", t)  # "2019-2021" is two years, not month 19
    if m:
        y = _year(m.group(2) if is_end else m.group(1))
        return date(y, 12 if is_end else 1, 1) if y else None
    m = re.search(r"\b(\d{4})\s*[-/.]\s*(\d{1,2})\b", t)  # ISO-ish "2019-05"
    if m and 1 <= int(m.group(2)) <= 12 and _year(m.group(1)):
        return date(int(m.group(1)), int(m.group(2)), 1)
    m = re.search(r"\b(\d{1,2})\s*[/-]\s*(\d{4})\b", t)
    if m and 1 <= int(m.group(1)) <= 12 and _year(m.group(2)):
        return date(int(m.group(2)), int(m.group(1)), 1)
    m = re.search(r"([a-z]{3})[a-z]*\.?,?\s+(\d{4})", t)
    if m and m.group(1) in _MONTHS and _year(m.group(2)):
        return date(int(m.group(2)), _MONTHS[m.group(1)], 1)
    m = re.search(r"\b(spring|summer|fall|autumn|winter)\b\s*,?\s*(\d{4})", t)
    if m and _year(m.group(2)):
        first, last = _SEASONS[m.group(1)]
        return date(int(m.group(2)), last if is_end else first, 1)
    m = re.search(r"\b(\d{4})\b", t)
    if m and _year(m.group(1)):
        return date(int(m.group(1)), 12 if is_end else 1, 1)
    return None


def _months_between(a: date, b: date) -> int:
    return (b.year - a.year) * 12 + (b.month - a.month)


def _scan(text: str) -> list[tuple[str, str]]:
    hits = []
    for pattern, suggestion in SENSITIVE_TERMS:
        for m in re.finditer(pattern, text or "", flags=re.IGNORECASE):
            hits.append((m.group(0), suggestion))
    return hits


def _reframe_hint(text: str) -> str:
    lowered = (text or "").lower()
    for stem, title in POSITIVE_REFRAMES.items():
        if stem in lowered:
            return title
    return ""


def review_profile(profile: dict[str, Any], gap_months: int = 9) -> dict[str, Any]:
    findings: list[dict[str, Any]] = []

    def check(location: str, text: str, field: str, context: str = "") -> None:
        for term, suggestion in _scan(text):
            finding = {"location": location, "field": field, "term": term, "text": text, "suggestion": suggestion,
                       "severity": "review"}
            # The job title and bullets say what the work was, even when the flagged text is the employer.
            hint = _reframe_hint(text + " " + context)
            if hint:
                finding["title_idea"] = hint
            findings.append(finding)

    check("Headline", profile.get("headline", ""), "headline")
    check("Summary", profile.get("summary", ""), "summary")
    for i, exp in enumerate(profile.get("experience", [])):
        label = f"Experience: {exp.get('title') or 'untitled'}"
        context = " ".join([exp.get("title", "")] + exp.get("bullets", []))
        check(label, exp.get("employer", ""), f"experience[{i}].employer", context)
        check(label, exp.get("title", ""), f"experience[{i}].title", context)
        check(label, exp.get("location", ""), f"experience[{i}].location", context)
        for j, bullet in enumerate(exp.get("bullets", [])):
            check(label, bullet, f"experience[{i}].bullets[{j}]", context)
    for i, vol in enumerate(profile.get("volunteer", [])):
        label = f"Volunteer: {vol.get('role') or 'untitled'}"
        check(label, vol.get("organization", ""), f"volunteer[{i}].organization")
        for j, bullet in enumerate(vol.get("bullets", [])):
            check(label, bullet, f"volunteer[{i}].bullets[{j}]")
    for i, edu in enumerate(profile.get("education", [])):
        check(f"Education: {edu.get('credential') or edu.get('school')}", edu.get("school", ""), f"education[{i}].school")
    for i, tr in enumerate(profile.get("training", [])):
        check(f"Training: {tr.get('name')}", tr.get("provider", ""), f"training[{i}].provider")

    gaps = find_gaps(profile, gap_months)
    return {
        "findings": findings,
        "gaps": gaps,
        "guidance": GUIDANCE,
        "summary": _summary(findings, gaps),
    }


def find_gaps(profile: dict[str, Any], gap_months: int = 9) -> list[dict[str, Any]]:
    """Gaps between any dated activity: jobs, volunteering, training and education all count."""
    spans = []
    for section, name_key in (("experience", "title"), ("volunteer", "role"), ("education", "credential"),
                              ("training", "name")):
        for entry in profile.get(section, []):
            # Education often has only a completion date: count it as that one month.
            start_raw = entry.get("start") or entry.get("date") or entry.get("end")
            start = parse_when(start_raw)
            end = parse_when(entry.get("end") or start_raw or "", is_end=True)
            if start and end and end >= start:
                spans.append((start, end, f"{entry.get(name_key) or section}"))
    spans.sort()
    gaps = []
    covered_until = None
    last_label = ""
    for start, end, label in spans:
        # Months with no activity in between: a job ending in Jan and the next starting in Feb is no gap.
        if covered_until and _months_between(covered_until, start) - 1 > gap_months:
            months = _months_between(covered_until, start) - 1
            gaps.append({
                "from": covered_until.strftime("%b %Y"), "to": start.strftime("%b %Y"), "months": months,
                "after": last_label, "before": label,
                "suggestion": "Add any training, programs, volunteering, caregiving or self-study from this period, "
                              "or show years only for nearby jobs.",
            })
        if covered_until is None or end > covered_until:
            covered_until, last_label = end, label
    return gaps


def _summary(findings: list, gaps: list) -> str:
    if not findings and not gaps:
        return "Looks good: nothing on this profile leads with setting or legal history, and no long gaps were found."
    parts = []
    if findings:
        parts.append(f"{len(findings)} spot(s) could lead with the skill instead of the setting")
    if gaps:
        parts.append(f"{len(gaps)} gap(s) could be filled with training, programs or volunteering")
    return "; ".join(parts) + ". These are suggestions — the participant decides."
