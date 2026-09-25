"""Coaching for interview questions about a criminal record.

A good answer to "Have you ever been convicted…?" has four moves, in order:

1. **Answer honestly** – say yes plainly and name it in general terms.
2. **Own it** – take responsibility without excuses, blame or minimizing.
3. **Show change** – concrete proof: programs, certificates, steady work, support.
4. **Turn to the job** – end on why you're ready for *this* job, in the present tense.

spaCy splits the answer into sentences and sorts each one into those moves (by
cue words, first-person subjects and verb tense), then checks for the things that
hurt these answers most: too much detail about the offense or the court case,
blaming others, minimizing, jail slang and talking too long. It also pulls proof
of change from the participant's profile (training, certificates, recent work).

It never coaches anyone to hide or misstate a record. If an employer asks
lawfully, the answer is the truth, told briefly and well.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from ..storage import NotFound, Store, as_text, new_id, utc_now
from .fair_chance import _scan as scan_sensitive
from .fair_chance import parse_when
from .nlp import NLP, article, straight_quotes

_DATA = Path(__file__).resolve().parent.parent / "data"
COLLECTION = "interview"

ELEMENTS = ["acknowledge", "ownership", "change", "pivot"]
ELEMENT_LABELS = {
    "acknowledge": "Answer honestly",
    "ownership": "Own it",
    "change": "Show change",
    "pivot": "Turn to the job",
}
ELEMENT_HINTS = {
    "acknowledge": "Start with a plain answer: “Yes. In 2019 I was convicted of …” — general terms, one sentence.",
    "ownership": "Take responsibility in your own words: “I made a bad decision and I own it.”",
    "change": "Give two or three specific things you've done since: programs, certificates, steady work, mentors.",
    "pivot": "End on the job: “…and I'm ready to bring that reliability to your team.”",
}
ELEMENT_WEIGHTS = {"acknowledge": 20, "ownership": 25, "change": 35, "pivot": 20}

ACKNOWLEDGE_CUES = [
    r"\byes\b", r"\bi (was|got|have been) (convicted|charged|arrested|sentenced|incarcerated)",
    r"\bi have (a|one|two|an?) (felony|misdemeanou?r|conviction|record|charge)", r"\bi was (incarcerated|in prison|in jail)",
    r"\bconvict(ed|ion)\b", r"\bfelony\b", r"\bmisdemeanou?r\b", r"\bmy record\b", r"\bthat'?s (from|correct)\b",
    r"\bi listed\b", r"\bi (did|served) (time|\d)", r"\bi'?m on (parole|probation)\b", r"\bi was on (parole|probation)\b",
    r"\bi (checked|marked) (the|that) box\b", r"\byeah\b", r"\bcaught a (case|charge)\b", r"\bdid (my|a) (bid|time)\b",
    r"\bi was (locked up|arrested)\b", r"\b(that'?s a )?fair (question|concern)\b", r"\bi understand (why|the concern)\b",
]
OWNERSHIP_CUES = [
    r"\b(take|took|taking) (full )?responsibility\b", r"\bi (made|was making) (a |some |a lot of |a few )?(bad|poor|wrong|serious)? ?"
    r"(mistakes?|decisions?|choices?)\b", r"\bi own\b", r"\bmy (fault|mistake|responsibility|actions|choices)\b",
    r"\baccountab", r"\bi regret\b", r"\bi('m| am) not proud\b", r"\bi accept(ed)?\b", r"\bi learned\b",
    r"\bi was wrong\b", r"\bno excuses?\b",
]
CHANGE_CUES = [
    r"\bsince (then|that|coming home|my release|being released|i got out)\b", r"\bcomplet(ed|ing)\b", r"\bearned\b",
    r"\bcertif", r"\bgraduat", r"\b(ged|hse|diploma)\b", r"\bprogram\b", r"\bclass(es)?\b", r"\bcourse\b",
    r"\bapprentice", r"\btraining\b", r"\bmentor", r"\bcounsel", r"\btreatment\b", r"\brecovery\b", r"\bsober",
    r"\bclean (for|since)\b", r"\bvolunteer", r"\bsteady\b", r"\b(held|kept|have) (a |my )?(steady |full-time |part-time )?job\b",
    r"\bworked\b", r"\bpromot", r"\bsupervisor\b", r"\breference", r"\btoday i\b", r"\bnow i\b", r"\bchanged\b",
    r"\bservsafe\b", r"\bforklift\b", r"\bosha\b", r"\bcdl\b", r"\bcheck in\b", r"\bschedule\b",
    r"\b(keys|register|drawer|balanced|trusted)\b",
]
PIVOT_CUES = [
    r"\bthis (job|role|position|company|team)\b", r"\byour (team|company|store|kitchen|crew|customers|business)\b",
    r"\bready (to|for)\b", r"\bi('d| would) (bring|love|like)\b", r"\bthat'?s why\b", r"\bexcited\b", r"\bwant to (grow|build|work)\b",
    r"\bi can (bring|offer|help)\b", r"\bgrow with\b", r"\bearn (the same|your)\b", r"\bprove\b", r"\bcount on me\b",
    r"\bwon'?t affect\b", r"\bavailab", r"\b(bring|contribute|work|grow)\b[^.]*\b(here|with you)\b",
]

# Details that pull the conversation back into the offense or the court case.
OVERSHARE = [
    r"\b(gun|pistol|firearm|knife(?! skills)|weapon|stab|stabbed|robbed|robbery|assault(ed)?|overdose)\b",
    r"\b(pounds?|grams?|ounces?|kilos?|bags?) of (cocaine|heroin|crack|meth|fentanyl|weed|marijuana|pills|drugs?|dope)\b",
    r"\b(cocaine|heroin|crack|meth|fentanyl|weed|pills)\b",
    r"\b(plea deal|pled|pleaded|plea|trial(?! period)|jury|judge|prosecutor|district attorney|\bda\b|public defender|lawyer|attorney|"
    r"appeal|arraign\w*|indict\w*|court date|sentencing|sentenced to \d+|years? (in|at) \w+ (prison|state)|"
    r"cell ?(mate|block)|solitary|the hole)\b",
]
BLAME = [
    r"\bwrong place\b", r"\bwrong time\b", r"\bset me up\b", r"\bnot my fault\b", r"\bthe cops?\b", r"\bpolice (were|was)\b",
    r"\bthe system (is|was) (rigged|unfair|against|broken|corrupt)", r"\bmy (lawyer|public defender) (didn'?t|never)\b",
    r"\bframed\b", r"\b(bad|wrong) crowd\b", r"\bthey (made|forced) me\b", r"\bunfair(ly)?\b", r"\bit wasn'?t (me|mine)\b",
    r"\beveryone (was|does) (doing )?(it|that)\b",
    r"\bbecause of (him|her|them|my (ex|friends|cousin|brother))\b",
]
MINIMIZE = [
    r"\b(it was )?(just|only) a (little|small|minor|misunderstanding)\b", r"\bno big deal\b", r"\bnot a big deal\b", r"\bminor thing\b",
    r"\beveryone does\b", r"\bit happens\b", r"\bbasically nothing\b", r"\bnobody got hurt\b", r"\bjust (some|a little)\b",
]
SLANG = {
    r"\bcaught a (case|charge)\b": "was charged", r"\bdid (a |my )?(bid|time)\b(?! management)": "was incarcerated", r"\blocked up\b": "incarcerated",
    r"\bthe feds\b": "federal", r"\bmy po\b": "my parole/probation officer",
    r"\bgot out (in|of prison|of jail)\b": "was released", r"\bthe yard\b": "the facility",
    r"\bthe c\.?o\.?s?\b(?!-)": "staff", r"\bpinched\b": "arrested",
}
HEDGES = ["kind of", "sort of", "i guess", "maybe", "i think", "probably", "you know", "basically", "stuff", "whatever"]
# "Mistakes were made" style: an event with no one owning it.
AGENTLESS = [r"\bmistakes were made\b", r"\bthings happened\b", r"\bstuff happened\b", r"\bit just happened\b",
             r"\bi got (caught|mixed) up\b", r"\bgot into (some )?trouble\b"]
LEGAL_NOTE = ("Know exactly what you have to disclose. Rules differ by state and by record: sealed, expunged or juvenile "
              "records and arrests that didn't lead to a conviction often don't have to be shared. Check with your "
              "outreach team before the interview. Never lie: background checks find it, and dishonesty is the most "
              "common reason an otherwise good candidate is turned down.")
# Saying there is no record. If that's true the participant doesn't need this practice; if there is a record the
# employer may lawfully ask about, a denial is the one answer that reliably loses the job.
DENIAL = [
    r"\b(never|not|haven'?t|have not|wasn'?t|was not|didn'?t|did not)\b[^.]{0,25}\b(convicted|arrested|charged|"
    r"incarcerated|in (jail|prison))\b",
    r"\b(my )?record is (clean|clear)\b", r"\bi (don'?t|do not) have (a|any) (criminal )?(record|convictions?|felon(y|ies))\b",
    r"\bno (criminal )?record\b", r"\b(left|didn'?t check|did not check)\b[^.]{0,20}\bbox\b", r"\bleft (it|that) blank\b",
]
DENIAL_CAP = 40
# Saying the report is wrong. That can be true (a sealed record that still shows up), but the interview isn't the
# place to argue it: background-check reports can be disputed with the company that made them.
DISPUTE = [r"\b(not|shouldn'?t|should not) (be )?(on|in) my record\b", r"\bshould(n'?t| not) be (there|on (it|there))\b",
           r"\bnot on my record anymore\b", r"\b(mistake|error|wrong) (on|in) (the|my|that) (report|record|check)\b",
           r"\b(the|that) (report|check) is wrong\b"]
_NUMBER = re.compile(r"\d|\b(one|two|three|four|five|six|seven|eight|nine|ten|twelve|dozen|hundred|once|twice|"
                     r"weekly|monthly|daily)\b", re.I)


def _any(patterns: list[str], text: str) -> list[str]:
    hits = []
    for p in patterns:
        m = re.search(p, text, re.I)
        if m:
            hits.append(m.group(0))
    return hits


def _grade(score: int) -> str:
    if score >= 85:
        return "Ready to say out loud"
    if score >= 70:
        return "Strong — polish a little"
    if score >= 50:
        return "Good start"
    return "Keep building"


class RecordCoach:
    def __init__(self, store: Store, nlp: NLP) -> None:
        self.store = store
        self.nlp = nlp
        with (_DATA / "record_questions.json").open("r", encoding="utf-8") as fh:
            self.questions = json.load(fh)["questions"]
        self._by_id = {q["id"]: q for q in self.questions}

    def question_bank(self) -> dict[str, Any]:
        return {"questions": self.questions, "legal_note": LEGAL_NOTE,
                "elements": [{"key": k, "label": ELEMENT_LABELS[k], "hint": ELEMENT_HINTS[k]} for k in ELEMENTS]}

    def _question(self, question_id: str, custom_question: str = "") -> dict[str, Any]:
        if question_id == "custom":
            custom_question = as_text(custom_question)
            if not custom_question.strip():
                raise ValueError("Type the question you want to practise.")
            return {"id": "custom", "category": "Custom", "question": custom_question.strip(), "looking_for": "",
                    "tip": "", "example": "", "moves": list(ELEMENTS)}
        q = self._by_id.get(question_id)
        if q is None:
            raise NotFound(f"question not found: {question_id}")
        return q

    # -- sentence analysis -------------------------------------------------------------------------
    def _classify(self, sent) -> list[str]:
        """Which of the four moves a sentence makes (it can make more than one)."""
        text = sent.text
        found = []
        if _any(ACKNOWLEDGE_CUES, text):
            found.append("acknowledge")
        if _any(OWNERSHIP_CUES, text):
            found.append("ownership")
        if _any(CHANGE_CUES, text) or self.nlp.find_skills(text):
            found.append("change")
        if _any(PIVOT_CUES, text):
            found.append("pivot")
        return found

    def _tense(self, sent) -> str:
        """'past', 'present' or 'future' for the sentence's main first-person verbs (full model only)."""
        if self.nlp.mode != "full":
            return ""
        tenses = []
        for tok in sent:
            if tok.pos_ in ("VERB", "AUX") and tok.dep_ in ("ROOT", "conj", "ccomp"):
                if any(c.lower_ in ("will", "'ll") for c in tok.children) or tok.lower_ in ("will", "'ll"):
                    tenses.append("future")
                elif "Past" in tok.morph.get("Tense"):
                    tenses.append("past")
                elif "Pres" in tok.morph.get("Tense") or tok.tag_ in ("VBP", "VBZ", "MD"):
                    tenses.append("present")
        if not tenses:
            return ""
        return "future" if "future" in tenses else ("present" if "present" in tenses else "past")

    # -- coaching ----------------------------------------------------------------------------------
    def coach(self, question_id: str, answer: str, custom_question: str = "", profile_id: str = "",
              save: bool = False) -> dict[str, Any]:
        q = self._question(question_id, custom_question)
        answer = re.sub(r"[ \t]+", " ", straight_quotes(answer or "")).strip()
        if len(answer.split()) < 3:
            raise ValueError("Write out your answer the way you'd say it, then ask for coaching.")
        doc = self.nlp.doc(answer)
        sentences = [s for s in doc.sents if s.text.strip()]
        words = len([t for t in doc if not (t.is_punct or t.is_space)])
        seconds = round(words / 140 * 60)

        rows = []
        present: dict[str, list[int]] = {k: [] for k in ELEMENTS}
        for i, sent in enumerate(sentences):
            moves = self._classify(sent)
            if _any(DENIAL, sent.text):
                moves = [m for m in moves if m != "acknowledge"]  # a denial isn't an honest answer
            for m in moves:
                present[m].append(i)
            rows.append({"text": sent.text.strip(), "moves": moves, "tense": self._tense(sent)})

        issues: list[dict[str, str]] = []

        def issue(kind: str, message: str, found: list[str] | None = None) -> None:
            issues.append({"kind": kind, "message": message, "found": ", ".join(dict.fromkeys(found or []))})

        denial = _any(DENIAL, answer)
        if denial:
            issue("denial", "This answer says there is no record. If that's true, say it plainly in one sentence. If there "
                            "is a record the employer is allowed to ask about, never deny it — background checks find it, "
                            "and it's the most common reason people lose the offer. If it was sealed or expunged, check "
                            "with your outreach team what you may say.", denial)
        if denial:
            present["acknowledge"] = []
        dispute = _any(DISPUTE, answer)
        if dispute:
            issue("dispute", "If the report shows something that was sealed, expunged or isn't yours, you have the right "
                             "to dispute it with the background-check company (under the Fair Credit Reporting Act), and "
                             "the employer must give you a copy of the report first. In the interview, stay calm: say "
                             "briefly that you're disputing it, then talk about today. Your outreach team can help.", dispute)
        # Offense detail and blame only count in sentences about the past, not in "since then" proof
        # ("learned knife skills", "moved 2,000 pounds of freight").
        past_text = " ".join(r["text"] for r in rows if not {"change", "pivot"} & set(r["moves"]))
        blame = _any(BLAME, past_text)
        if blame:
            issue("blame", "This can sound like blaming others. Even if you feel it was unfair, the interview isn't the "
                           "place to argue the case — keep the focus on what you control.", blame)
        minimize = _any(MINIMIZE, past_text)
        if minimize:
            issue("minimize", "Avoid making it sound small. Employers hear minimizing as not taking it seriously; a "
                              "plain, honest description lands better.", minimize)
        agentless = _any(AGENTLESS, answer)
        if agentless:
            issue("agentless", "Say it with “I”: “I made a bad decision” owns it; “mistakes were made” or “I got "
                               "caught up” pushes it away.", agentless)
        overshare = _any(OVERSHARE, past_text)
        if overshare:
            issue("overshare", "Too much detail about the offense or the court case. Name it in general terms (e.g. "
                               "“a drug-related offense”) and move on; details invite more questions.", overshare)
        slang = [m for p in SLANG if (m := re.search(p, answer, re.I))]
        if slang:
            swaps = "; ".join(f"“{m.group(0)}” → “{SLANG[p]}”" for p in SLANG
                              for m in [re.search(p, answer, re.I)] if m)
            issue("slang", "Use plain, professional words: " + swaps + ".", [m.group(0) for m in slang])
        hedges = [h for h in HEDGES if re.search(rf"(?<![a-z]){re.escape(h)}(?![a-z])", answer.lower())]
        if hedges:
            issue("hedge", "Cut filler that makes you sound unsure.", hedges)
        if re.search(r"\b(sealed|expunged|juvenile)\b|\b(charges?|case) (was|were|got) (dismissed|dropped)\b", answer, re.I):
            issue("legal", "You mention a sealed, expunged, juvenile or dismissed matter. In many places you don't have "
                           "to disclose these — check with your outreach team before sharing it in an interview.")
        if words < 35:
            issue("short", "A little short. Aim for 30–60 seconds (about 70–140 words) so there's room for what "
                           "you've done since.")
        elif words > 190:
            issue("long", "Too long. Past about 90 seconds it starts to sound like a story about the offense. Keep "
                          "the past to one or two sentences and trim.")

        # Where the weight of the answer sits: most sentences should be about change and the job.
        past_heavy = False
        if len(sentences) >= 3:
            # Weigh by words, so a one-word "Yes." doesn't count like a whole story.
            about_past = sum(len(r["text"].split()) for r in rows if not {"change", "pivot"} & set(r["moves"]))
            if about_past > words / 2:
                past_heavy = True
                issue("balance", "Most of the answer is about the past. A good rule: one or two sentences on what "
                                 "happened, the rest on what you've done since and why you're ready.")
        if rows and self.nlp.mode == "full" and rows[-1]["tense"] == "past" and "pivot" not in rows[-1]["moves"]:
            issue("ending", "End in the present or future (“Today I…”, “I'm ready to…”), not in the past.")
        needed = [k for k in ELEMENTS if k in q.get("moves", ELEMENTS)]
        if ("acknowledge" in needed and present["acknowledge"] and present["change"]
                and min(present["change"]) < min(present["acknowledge"])):
            issue("order", "Put the honest answer first. Leading with good news can sound like you're avoiding the question.")

        # Element scores.
        elements = []
        change_numbers = any(_NUMBER.search(rows[i]["text"]) for i in present["change"])
        change_count = len(present["change"])
        score = 0.0
        for key in needed:
            hit = bool(present[key])
            feedback = ""
            part = 1.0 if hit else 0.0
            if key == "acknowledge" and not hit:
                feedback = ELEMENT_HINTS[key]
            elif key == "ownership":
                if not hit:
                    feedback = ELEMENT_HINTS[key]
                elif blame or minimize or agentless:
                    part, feedback = 0.4, "You take some ownership, but other words undercut it (see below)."
            elif key == "change":
                if not hit:
                    feedback = ELEMENT_HINTS[key]
                elif change_count < 2 and not change_numbers:
                    part, feedback = 0.6, "Add one more specific proof, with a name or number (hours, months, certificate)."
                elif not change_numbers:
                    part, feedback = 0.85, "Add a number: how long you've worked, program hours, months without a missed shift."
            elif key == "pivot":
                if not hit:
                    feedback = ELEMENT_HINTS[key]
                elif rows and "pivot" not in rows[-1]["moves"]:
                    part, feedback = 0.7, "Move the link to the job to the very end, so it's the last thing they hear."
            score += ELEMENT_WEIGHTS[key] * part
            elements.append({"key": key, "label": ELEMENT_LABELS[key], "present": hit, "score": round(part * 100),
                             "feedback": feedback,
                             "sentences": [rows[i]["text"] for i in present[key]]})
        penalty = {"blame": 12, "minimize": 8, "agentless": 6, "overshare": 10, "slang": 4, "hedge": 3,
                   "short": 6, "long": 8, "dispute": 8, "balance": 6, "ending": 4, "order": 4}
        score = score * 100 / sum(ELEMENT_WEIGHTS[k] for k in needed)  # only the moves this question calls for
        score -= sum(penalty.get(i["kind"], 0) for i in issues)
        if denial:
            score = min(score, DENIAL_CAP)
        score = max(0, min(100, round(score)))

        result = {
            "question": q, "score": score, "grade": _grade(score), "elements": elements, "issues": issues,
            "sentences": rows, "word_count": words, "speaking_seconds": seconds, "legal_note": LEGAL_NOTE,
            "proof": self.proof_points(profile_id) if profile_id else [],
            "outline": self._outline(rows, present, needed),
            "past_heavy": past_heavy,
        }
        weakest = min(elements, key=lambda e: e["score"])
        result["next_step"] = (f"Next: {weakest['label'].lower()}. {weakest['feedback']}" if weakest["feedback"]
                               else (f"Next: {issues[0]['message']}" if issues else "Practise it out loud until it feels natural."))
        if save:
            record = {"id": new_id("int"), "type": "record", "profile_id": profile_id, "question_id": q["id"],
                      "question": q["question"], "answer": answer, "score": score, "created_at": utc_now()}
            self.store.put(COLLECTION, record["id"], record)
            self.store.append_history("interview", f"Practised a record question: “{q['question'][:60]}” ({score}%)",
                                      record_id=record["id"], profile_id=profile_id)
            result["record"] = record
        return result

    @staticmethod
    def _outline(rows: list[dict], present: dict[str, list[int]], needed: list[str]) -> list[dict[str, Any]]:
        """The participant's own sentences re-ordered into the four moves, with gaps marked."""
        used: set[int] = set()
        out = []
        for key in needed:
            idx = [i for i in present[key] if i not in used]
            used.update(idx)
            out.append({"key": key, "label": ELEMENT_LABELS[key], "sentences": [rows[i]["text"] for i in idx],
                        "missing": not present[key], "hint": ELEMENT_HINTS[key]})
        return out

    # -- proof of change from the profile ------------------------------------------------------------
    def proof_points(self, profile_id: str) -> list[dict[str, str]]:
        """Things the participant has done that make good 'since then' evidence, newest first."""
        profile = self.store.get("profiles", profile_id)
        if profile is None:
            return []
        points: list[tuple[str, dict[str, str]]] = []

        def when(entry: dict, *keys: str) -> str:
            for k in keys:
                d = parse_when(entry.get(k, ""), is_end=True)
                if d:
                    return d.isoformat()
            return ""

        def add(sort_key: str, kind: str, text: str, sentence: str) -> None:
            # Proof should name the program or job, not the setting.
            if text and not scan_sensitive(text):
                points.append((sort_key, {"kind": kind, "text": text, "sentence": sentence}))

        for c in profile.get("certifications", []):
            if c.get("name"):
                add(when(c, "date"), "Certification", c["name"],
                    f"I earned my {c['name']}" + (f" in {c['date']}" if c.get("date") else "") + ".")
        for t in profile.get("training", []):
            if t.get("name"):
                name, hours = t["name"], str(t.get("hours", "")).strip()
                program = name.split()[-1].lower() in ("program", "programme", "course", "class", "training",
                                                       "apprenticeship", "academy", "workshop", "bootcamp")
                if program:  # "a 240-hour Pre-Apprenticeship Construction Program"
                    lead = f"{hours}-hour " if hours.isdigit() else ""
                    sentence = f"I completed {article(lead or name)} {lead}{name}."
                else:  # "OSHA 10 (10 hours)", not "a 10-hour OSHA 10"
                    sentence = f"I completed {name}" + (f" ({hours} hours)." if hours.isdigit() else ".")
                add(when(t, "date", "end"), "Training", name, sentence)
        for e in profile.get("education", []):
            if e.get("credential") or e.get("school"):
                add(when(e, "end", "start"), "Education", e.get("credential") or e["school"],
                    f"I earned my {e['credential']}." if e.get("credential") else f"I studied at {e['school']}.")
        for x in profile.get("experience", []):
            if x.get("title"):
                current = str(x.get("end", "")).strip().lower() in ("present", "current", "now", "")
                employer = x.get("employer", "")
                where = f" at {employer}" if employer and not scan_sensitive(employer) else ""
                role = f"{article(x['title'])} {x['title']}"
                sentence = (f"I work as {role}{where}." if current else f"I worked as {role}{where}.")
                add(when(x, "end", "start"), "Work", x["title"] + where, sentence)
        for v in profile.get("volunteer", []):
            if v.get("role"):
                org = v.get("organization", "")
                add(when(v, "end", "start"), "Volunteer", v["role"],
                    f"I volunteer as {article(v['role'])} {v['role']}" + (f" with {org}" if org and not scan_sensitive(org) else "") + ".")
        points.sort(key=lambda p: p[0], reverse=True)
        return [p for _, p in points[:6]]
