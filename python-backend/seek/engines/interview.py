"""Interview coaching: S.T.A.R.S answers and workplace-assessment practice.

S.T.A.R.S = Situation, Task, Action, Result, Skills. It is the classic STAR
behavioral-interview method plus a closing "Skills" step: name what the story
proves, in the employer's words.

The personality practice mirrors the agree/disagree assessments many hourly
employers put in online applications. It is coaching, not a psychological
test: it explains what employers look for, flags answers that contradict each
other or sound too good to be true, and always tells people to answer honestly.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from ..storage import NotFound, Store, as_text, new_id, utc_now
from .fair_chance import _scan as scan_sensitive
from .nlp import NLP, STRONG_VERBS, needs_exact, stem

_DATA = Path(__file__).resolve().parent.parent / "data"
COLLECTION = "interview"
PARTS = ["situation", "task", "action", "result", "skills"]
PART_LABELS = {"situation": "Situation", "task": "Task", "action": "Action", "result": "Result", "skills": "Skills"}
PART_WEIGHTS = {"situation": 0.15, "task": 0.15, "action": 0.35, "result": 0.25, "skills": 0.10}
PART_HINTS = {
    "situation": "Where were you and what was going on? One or two sentences.",
    "task": "What did you need to do, or what was the goal or problem?",
    "action": "What did YOU do, step by step? This is the heart of the answer.",
    "result": "What happened because of it? Use numbers if you can.",
    "skills": "What skills does this story prove, and how would they help in this job?",
}
# (min, ideal max) word counts per part.
PART_LENGTH = {"situation": (12, 60), "task": (8, 45), "action": (30, 120), "result": (10, 60), "skills": (8, 50)}

HEDGES = ["kind of", "sort of", "i guess", "maybe", "i think", "probably", "stuff", "things like that", "whatever",
          "you know", "basically", "pretty much"]
NEGATIVE = ["stupid", "idiot", "hate", "lazy", "his fault", "her fault", "their fault", "not my fault", "unfair",
            "dumb", "useless", "screwed", "pissed", "worst"]
RESULT_WORDS = ["saved", "reduced", "increased", "improved", "finished", "completed", "passed", "zero", "praised",
                "promoted", "recognized", "thanked", "on time", "ahead of", "fewer", "more", "faster", "record",
                "learned", "resolved", "fixed", "kept", "earned", "certified", "hired", "trusted"]
TASK_CUES = ["had to", "needed to", "need to", "my job", "my role", "responsible", "goal", "asked to", "task", "problem",
             "was to", "supposed to", "wanted to", "challenge"]
_NUMBER = re.compile(r"\d|\b(one|two|three|four|five|six|seven|eight|nine|ten|dozen|hundred|thousand|half|twice)\b", re.I)
SCALE = ["Strongly disagree", "Disagree", "Neutral", "Agree", "Strongly agree"]


def _load(name: str) -> dict[str, Any]:
    with (_DATA / name).open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _find(text: str, phrases: list[str]) -> list[str]:
    lowered = f" {text.lower()} "
    return [p for p in phrases if re.search(rf"(?<![a-z]){re.escape(p)}(?![a-z])", lowered)]


def _grade(score: int) -> str:
    if score >= 85:
        return "Interview-ready"
    if score >= 72:
        return "Strong — polish a little"
    if score >= 50:
        return "Good start"
    return "Keep building"


def _sentence(text: str) -> str:
    text = re.sub(r"\s+", " ", text or "").strip()
    if not text:
        return ""
    text = text[0].upper() + text[1:]
    return text if text[-1] in ".!?" else text + "."


class InterviewEngine:
    def __init__(self, store: Store, nlp: NLP) -> None:
        self.store = store
        self.nlp = nlp
        self.stars = _load("stars_questions.json")["questions"]
        self.assessment = _load("assessment.json")
        self._by_id = {q["id"]: q for q in self.stars}
        self._items = {i["id"]: i for i in self.assessment["items"]}

    # -- question banks ----------------------------------------------------------------------
    def stars_questions(self) -> dict[str, Any]:
        return {"questions": self.stars, "parts": [{"key": p, "label": PART_LABELS[p], "hint": PART_HINTS[p]} for p in PARTS]}

    def assessment_items(self) -> dict[str, Any]:
        return {"traits": self.assessment["traits"], "items": self.assessment["items"], "scale": SCALE}

    def _question(self, question_id: str, custom_question: str = "") -> dict[str, Any]:
        if question_id == "custom" or (not question_id and custom_question):
            custom_question = as_text(custom_question)
            if not custom_question.strip():
                raise ValueError("Type the interview question to practise.")
            return {"id": "custom", "category": "Custom", "question": custom_question.strip(), "looking_for": "",
                    "keywords": [], "tip": ""}
        q = self._by_id.get(question_id)
        if q is None:
            raise NotFound(f"question not found: {question_id}")
        return q

    # -- story ideas from the profile ------------------------------------------------------
    def story_ideas(self, profile_id: str, question_id: str, custom_question: str = "") -> list[dict[str, Any]]:
        """Bullets from the participant's own profile that could anchor an answer."""
        profile = self.store.get("profiles", profile_id)
        if profile is None:
            raise NotFound(f"profile not found: {profile_id}")
        q = self._question(question_id, custom_question)
        cue_stems = {stem(w) for kw in q["keywords"] for w in self.nlp.key(kw).split()}
        cue_stems |= {stem(w) for w in self.nlp.key(q["question"]).split() if len(w) > 3}
        ideas = []
        for section, role_key, org_key in (("experience", "title", "employer"), ("volunteer", "role", "organization")):
            for entry in profile.get(section, []):
                for bullet in entry.get("bullets", []):
                    prof = self.nlp.profile_text(bullet)
                    hits = sorted(cue_stems & (prof.stems | {stem(w) for w in prof.lemmas}))
                    score = len(hits) + (0.5 if _NUMBER.search(bullet) else 0)
                    ideas.append({"text": bullet, "role": entry.get(role_key, ""), "org": entry.get(org_key, ""),
                                  "score": score, "hits": hits, "starter": self._starter(entry, role_key, org_key, bullet)})
        for tr in profile.get("training", []):
            if tr.get("name"):
                text = f"Completed {tr['name']}" + (f" ({tr['hours']} hours)" if tr.get("hours") else "")
                score = 1.5 if q["id"] in ("learn_fast", "gap", "why_hire") else 0.2
                ideas.append({"text": text, "role": "Training", "org": tr.get("provider", ""), "score": score, "hits": [],
                              "starter": {"situation": f"I enrolled in {tr['name']}" + (f" with {tr['provider']}" if tr.get("provider") else "") + ".",
                                          "action": "", "result": text + "."}})
        ideas.sort(key=lambda i: -i["score"])
        return [i for i in ideas if i["score"] > 0][:5]

    @staticmethod
    def _starter(entry: dict, role_key: str, org_key: str, bullet: str) -> dict[str, str]:
        role, org = entry.get(role_key, ""), entry.get(org_key, "")
        where = f"While working as {role}" + (f" with {org}" if org else "") if role else (f"At {org}" if org else "At work")
        clause = bullet.strip().rstrip(".")
        clause = clause[0].lower() + clause[1:] if clause and not clause.split(" ")[0].isupper() else clause
        return {"situation": f"{where}, …", "action": f"I {clause}.", "result": _sentence(bullet) if _NUMBER.search(bullet) else ""}

    # -- S.T.A.R.S coaching ------------------------------------------------------------------
    def coach(self, question_id: str, answers: dict[str, str], custom_question: str = "", job_id: str = "",
              profile_id: str = "", save: bool = False) -> dict[str, Any]:
        q = self._question(question_id, custom_question)
        answers = {p: str((answers or {}).get(p, "") or "").strip() for p in PARTS}
        if not any(answers.values()):
            raise ValueError("Write at least one part of the answer to get coaching.")
        job = self.store.get("jobs", job_id) if job_id else None
        parts = {p: self._coach_part(p, answers[p], q, job) for p in PARTS}

        # Whole-answer checks.
        full = " ".join(answers[p] for p in PARTS if answers[p])
        overall_notes: list[str] = []
        hedges = _find(full, HEDGES)
        if hedges:
            overall_notes.append("Cut filler words that make you sound unsure: " + ", ".join(f"“{h}”" for h in hedges[:4]) + ".")
        negative = _find(full, NEGATIVE)
        if negative:
            overall_notes.append("Keep it positive. Words like " + ", ".join(f"“{n}”" for n in negative[:3]) +
                                 " can sound like blaming, even when you were in the right.")
        sensitive = scan_sensitive(full)
        if sensitive:
            if q.get("sensitive"):
                overall_notes.append("For this question it's right to be honest about your background. Keep that part to one or "
                                     "two sentences, then spend the rest on growth and readiness.")
            else:
                overall_notes.append("This story mentions the setting (" + sensitive[0][0] + "). Program and crew work are real "
                                     "experience; lead with the job and what you did rather than where it happened.")
        words = len(full.split())
        seconds = round(words / 140 * 60)
        if words < 90:
            overall_notes.append("The whole answer is short. Aim for about 1–2 minutes spoken (150–280 words).")
        elif words > 380:
            overall_notes.append("The whole answer is long. Aim for about 2 minutes spoken; trim the Situation first.")

        score = round(sum(parts[p]["score"] * PART_WEIGHTS[p] for p in PARTS))
        score -= 5 * min(len(hedges), 2) + 8 * min(len(negative), 2)
        if words < 90:
            score -= 8  # a thin answer shouldn't read as interview-ready even if every box is filled
        score = max(0, score)
        weakest = min(PARTS, key=lambda p: parts[p]["score"])
        result = {
            "question": q, "parts": parts, "score": score, "grade": _grade(score), "notes": overall_notes,
            "word_count": words, "speaking_seconds": seconds,
            "next_step": f"Next, strengthen your {PART_LABELS[weakest]}: " + (parts[weakest]["feedback"] or [PART_HINTS[weakest]])[0],
            "polished": self._polish(answers),
        }
        if save:
            record = {"id": new_id("int"), "type": "stars", "profile_id": profile_id, "job_id": job_id,
                      "question_id": q["id"], "question": q["question"], "answers": answers, "score": score,
                      "polished": result["polished"], "created_at": utc_now()}
            self.store.put(COLLECTION, record["id"], record)
            self.store.append_history("interview", f"Practised STARS answer: “{q['question'][:60]}” ({score}%)",
                                      record_id=record["id"], profile_id=profile_id)
            result["record"] = record
        return result

    def _coach_part(self, part: str, text: str, q: dict, job: dict | None) -> dict[str, Any]:
        feedback: list[str] = []
        good: list[str] = []
        if not text:
            return {"score": 0, "words": 0, "feedback": [f"Missing. {PART_HINTS[part]}"], "good": []}
        doc = self.nlp.doc(text)
        words = len([t for t in doc if not t.is_punct and not t.is_space])
        lo, hi = PART_LENGTH[part]
        score = 100
        if words < lo:
            feedback.append(f"Add a little more detail (about {lo}+ words).")
            score -= 25
        elif words > hi:
            feedback.append(f"Tighten this part (under ~{hi} words) so the Action and Result get the spotlight.")
            score -= 15

        lowered = text.lower()
        if part == "situation":
            if not re.search(r"\b(at|when|while|during|in my|last|ago|20\d\d)\b", lowered):
                feedback.append("Set the scene: say where and when (e.g. “While working as a line cook at…”).")
                score -= 15
            else:
                good.append("Clear setting.")
        elif part == "task":
            if not _find(text, TASK_CUES):
                feedback.append("Say what you needed to do or what the goal was (e.g. “I had to…”, “My job was to…”).")
                score -= 20
            else:
                good.append("The goal is clear.")
        elif part == "action":
            i_count = len(re.findall(r"\bI\b", text))
            we_count = len(re.findall(r"\bwe\b", text, re.I))
            verbs = self._first_person_verbs(text)
            if we_count > i_count:
                feedback.append("Use “I” more than “we”. The interviewer is hiring you, so say what you personally did.")
                score -= 25
            elif i_count:
                good.append("Owns the actions with “I”.")
            if len(verbs) < 2:
                feedback.append("Describe at least two concrete steps you took, with strong verbs (e.g. "
                                + ", ".join(STRONG_VERBS[10:14]).lower() + ").")
                score -= 20
            else:
                good.append("Concrete steps: " + ", ".join(verbs[:4]) + ".")
            if len(list(doc.sents)) < 2 and words >= lo:
                feedback.append("Break it into steps: first…, then…, finally….")
                score -= 10
        elif part == "result":
            if _NUMBER.search(text):
                good.append("Uses a number — results stick.")
            else:
                feedback.append("Add a number if you can: how many, how much, how fast, how often.")
                score -= 20
            if _find(text, RESULT_WORDS):
                good.append("Shows a real outcome.")
            else:
                feedback.append("Say what changed or improved because of what you did.")
                score -= 15
        elif part == "skills":
            skills = list(self.nlp.find_skills(text))
            if skills:
                good.append("Names skills: " + ", ".join(skills[:4]) + ".")
            else:
                feedback.append("Name 1–3 specific skills this proves (e.g. teamwork, customer service, forklift, safety).")
                score -= 25
            if job:
                kws = job.get("keywords", [])[:25]
                job_keys = {k["key"]: k["term"] for k in kws}
                prof = self.nlp.profile_text(text)
                hit = [k["term"] for k in kws if self.nlp.covers(prof, k["key"], strict=needs_exact(k))]
                if hit:
                    good.append("Connects to the posting: " + ", ".join(hit[:4]) + ".")
                else:
                    feedback.append("Tie it to the job: use one or two words from the posting, e.g. " +
                                    ", ".join(list(job_keys.values())[:3]) + ".")
                    score -= 15
            if not re.search(r"\b(this job|this role|your|here|for you|help (you|your|the team))\b", lowered):
                feedback.append("Finish by linking it to this job: “…and that's what I'd bring to your team.”")
                score -= 10
        return {"score": max(0, min(100, score)), "words": words, "feedback": feedback, "good": good}

    def _first_person_verbs(self, text: str) -> list[str]:
        doc = self.nlp.doc(text)
        verbs = []
        for t in doc:
            if t.pos_ in ("VERB",) and any(c.lower_ == "i" and c.dep_ in ("nsubj", "nsubjpass") for c in t.children):
                verbs.append(t.text.lower())
            elif self.nlp.mode != "full" and t.text.capitalize() in STRONG_VERBS:
                verbs.append(t.text.lower())
        # Verbs coordinated with an "I" verb ("I checked and fixed") count too.
        for t in doc:
            if t.dep_ == "conj" and t.pos_ == "VERB" and t.head.text.lower() in verbs and t.text.lower() not in verbs:
                verbs.append(t.text.lower())
        return list(dict.fromkeys(verbs))

    @staticmethod
    def _polish(answers: dict[str, str]) -> str:
        return " ".join(_sentence(answers[p]) for p in PARTS if answers.get(p))

    # -- assessment -------------------------------------------------------------------------------
    def score_assessment(self, answers: dict[str, Any], profile_id: str = "", save: bool = False) -> dict[str, Any]:
        clean: dict[str, int] = {}
        for item_id, value in (answers or {}).items():
            if item_id in self._items:
                try:
                    v = int(value)
                except (TypeError, ValueError):
                    continue
                if 1 <= v <= 5:
                    clean[item_id] = v
        if not clean:
            raise ValueError("Answer at least one statement first.")

        per_trait: dict[str, list[int]] = {}
        item_feedback = []
        for item_id, v in clean.items():
            item = self._items[item_id]
            keyed = 6 - v if item.get("reverse") else v
            per_trait.setdefault(item["trait"], []).append(keyed)
            if item.get("absolute"):
                fit = "concern" if v == 5 else "strong" if v in (3, 4) else "ok"
            elif item["trait"] == "integrity" and item.get("reverse") and v >= 3:
                fit = "concern"
            else:
                fit = "strong" if keyed >= 4 else "ok" if keyed == 3 else "concern"
            item_feedback.append({"id": item_id, "text": item["text"], "trait": item["trait"], "answer": v,
                                  "answer_label": SCALE[v - 1], "fit": fit, "coaching": item["coaching"]})

        traits = []
        for key, meta in self.assessment["traits"].items():
            vals = per_trait.get(key)
            if not vals:
                continue
            score = round((sum(vals) / len(vals) - 1) / 4 * 100)
            traits.append({"key": key, "label": meta["label"], "about": meta["about"], "score": score, "answered": len(vals)})
        traits.sort(key=lambda t: -t["score"])

        flags = []
        for item in self.assessment["items"]:
            other = item.get("pair")
            if other and item["id"] in clean and other in clean:
                a_item, b_item = item, self._items[other]
                a = 6 - clean[a_item["id"]] if a_item.get("reverse") else clean[a_item["id"]]
                b = 6 - clean[b_item["id"]] if b_item.get("reverse") else clean[b_item["id"]]
                if abs(a - b) >= 3:
                    flags.append({"kind": "inconsistent", "items": [b_item["id"], a_item["id"]],
                                  "message": f"“{b_item['text']}” and “{a_item['text']}” were answered in ways that "
                                             "contradict each other. Real assessments notice this. Re-read both and answer "
                                             "the way you truly work."})
        absolutes = [i for i in item_feedback if self._items[i["id"]].get("absolute") and i["answer"] == 5]
        if absolutes:
            flags.append({"kind": "too_good", "items": [i["id"] for i in absolutes],
                          "message": "“Strongly agree” with never/always statements can look too good to be true. "
                                     "Honest, realistic answers score better on these."})
        if len(clean) >= 8 and len(set(clean.values())) == 1:
            flags.append({"kind": "same_answer", "items": list(clean),
                          "message": "Every statement got the same answer. Read each one: some are worded the opposite way."})
        integrity_red = [i for i in item_feedback if i["trait"] == "integrity" and self._items[i["id"]].get("reverse")
                         and i["answer"] >= 3]
        if integrity_red:
            flags.append({"kind": "integrity", "items": [i["id"] for i in integrity_red],
                          "message": "Some honesty statements weren't clearly disagreed with. Many employers screen these "
                                     "strictly, so think about what each one really asks."})

        strengths = [t["label"] for t in traits if t["score"] >= 75]
        growth = [t["label"] for t in traits if t["score"] < 50]
        tips = []
        if strengths:
            tips.append("Strengths to bring up in interviews: " + ", ".join(strengths[:3]) +
                        ". Prepare a STARS story for each one.")
        if growth:
            tips.append("Areas to think about: " + ", ".join(growth[:3]) +
                        ". Read the coaching for those statements, and talk them through with your outreach team.")
        tips.append("On a real assessment: read every statement fully, watch for opposite wording, avoid "
                    "“never/always” extremes, and answer honestly. Consistency matters more than any single answer.")
        result = {
            "answered": len(clean), "total": len(self._items), "traits": traits, "flags": flags,
            "items": item_feedback, "strengths": strengths, "growth": growth, "tips": tips,
            "consistency": "Consistent" if not any(f["kind"] == "inconsistent" for f in flags) else "Some contradictions",
        }
        if save:
            record = {"id": new_id("int"), "type": "assessment", "profile_id": profile_id, "answers": clean,
                      "traits": traits, "flags": len(flags), "created_at": utc_now()}
            self.store.put(COLLECTION, record["id"], record)
            self.store.append_history("interview", f"Completed practice assessment ({len(clean)} statements, "
                                      f"{len(flags)} flag(s))", record_id=record["id"], profile_id=profile_id)
            result["record"] = record
        return result

    # -- saved practice -----------------------------------------------------------------------------
    def saved(self, profile_id: str = "", type: str = "") -> list[dict[str, Any]]:  # noqa: A002 - contract name
        rows = [r for r in self.store.all(COLLECTION)
                if (not profile_id or r.get("profile_id") == profile_id) and (not type or r.get("type") == type)]
        rows.sort(key=lambda r: r.get("created_at", ""), reverse=True)
        return rows

    def delete(self, record_id: str) -> dict[str, Any]:
        if not self.store.delete(COLLECTION, record_id):
            raise NotFound(f"practice record not found: {record_id}")
        return {"deleted": record_id}
