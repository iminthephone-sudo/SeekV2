"""spaCy language layer shared by every SEEK engine.

One ``NLP`` instance is created per bridge process (loading a pipeline takes a
second or two, so it is loaded once and reused). If the configured model is
not installed SEEK still works in *basic* mode — a blank English pipeline plus
rule-based fallbacks — and reports that through ``system.status`` so the shell
can tell staff how to install the full model.
"""

from __future__ import annotations

import json
import os
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Iterable

import spacy
from spacy.matcher import PhraseMatcher

DEFAULT_MODEL = os.environ.get("SEEK_SPACY_MODEL", "en_core_web_sm")
_DATA = Path(__file__).resolve().parent.parent / "data"

# Words that appear in nearly every posting and say nothing about the job.
BOILERPLATE = {
    "ability", "abilities", "applicant", "applicants", "application", "benefit", "benefits", "candidate",
    "candidates", "career", "company", "day", "days", "description", "disability", "duty", "duties",
    "employee", "employees", "employer", "employment", "equal opportunity", "equal opportunity employer",
    "experience", "hour", "hours", "individual", "individuals", "job", "jobs", "knowledge", "level", "life",
    "lot", "member", "members", "opportunity", "opportunities", "organization", "people", "person", "place",
    "position", "positions", "qualification", "qualifications", "requirement", "requirements",
    "responsibility", "responsibilities", "role", "skill", "skills", "status", "team", "team member", "thing",
    "time", "today", "veteran", "veterans", "way", "week", "weeks", "work", "workplace", "year", "years",
    "you", "we", "us", "they", "it", "who", "what", "which", "all", "others", "other", "pay", "salary",
    "wage", "wages", "shift", "shifts", "information", "area", "areas", "environment", "world", "part",
    "question", "questions", "click", "apply", "age", "race", "religion", "gender", "sex", "national origin",
    "orientation", "law", "anything", "something", "everything", "one", "ones", "plus", "etc",
    "lbs", "pound", "pounds", "hiring",
}

# Filler adjectives stripped from the front of noun chunks ("strong teamwork" -> "teamwork").
GENERIC_MODIFIERS = {
    "strong", "excellent", "good", "great", "reliable", "proven", "solid", "basic", "some", "able",
    "various", "new", "successful", "ideal", "motivated", "dependable", "outstanding", "exceptional",
}

# Sentences containing these read as hard requirements, so their keywords weigh more.
REQUIRED_CUES = ("must", "required", "requires", "require", "minimum", "need", "needs", "necessary", "mandatory")
PREFERRED_CUES = ("preferred", "a plus", "nice to have", "bonus", "desired", "ideally")

WEAK_OPENERS = {
    "responsible for": "Start with what you did: 'Managed…', 'Operated…', 'Trained…'.",
    "duties included": "Replace with an action verb that shows ownership.",
    "helped with": "Say what you contributed: 'Supported…', 'Assisted 5 staff to…'.",
    "worked on": "Name the outcome: 'Built…', 'Repaired…', 'Completed…'.",
    "in charge of": "Try 'Led…', 'Supervised…', or 'Coordinated…'.",
    "tasked with": "Lead with the verb for the task itself.",
    "was involved in": "Say exactly what your part was.",
    "various": "Be specific — name the tasks, tools or numbers.",
    "etc": "List the real items instead of 'etc.'.",
}

STRONG_VERBS = [
    "Achieved", "Assembled", "Built", "Cleaned", "Completed", "Coordinated", "Delivered", "Drove", "Earned",
    "Fixed", "Handled", "Improved", "Installed", "Led", "Loaded", "Maintained", "Managed", "Mentored",
    "Operated", "Organized", "Packed", "Prepared", "Processed", "Reduced", "Repaired", "Resolved", "Served",
    "Stocked", "Supervised", "Supported", "Trained", "Volunteered",
]

_WORD = re.compile(r"[A-Za-z][A-Za-z+#./'-]*[A-Za-z+#]|[A-Za-z]")
_NUMBER = re.compile(r"\d|\b(one|two|three|four|five|six|seven|eight|nine|ten|dozen|hundred|thousand)\b", re.I)


@dataclass
class Keyword:
    term: str
    key: str
    score: float
    kind: str  # "skill" | "phrase"
    category: str = ""
    required: bool = False
    count: int = 1

    def to_dict(self) -> dict:
        return {
            "term": self.term, "key": self.key, "score": round(self.score, 2), "kind": self.kind,
            "category": self.category, "required": self.required, "count": self.count,
        }


def stem(word: str) -> str:
    """Crude suffix stem, a safety net for words the tagger leaves unlemmatised ("unloaded" as ADJ)."""
    for suffix in ("ing", "ed", "es", "s"):
        if word.endswith(suffix) and len(word) - len(suffix) >= 3:
            return word[: -len(suffix)]
    return word


@dataclass
class TextProfile:
    """Everything the matcher needs to know about a block of text."""

    lemmas: set[str] = field(default_factory=set)
    stems: set[str] = field(default_factory=set)
    phrases: set[str] = field(default_factory=set)
    skills: set[str] = field(default_factory=set)


class NLP:
    def __init__(self, model: str = DEFAULT_MODEL) -> None:
        self.model_name = model
        self.mode = "basic"
        self.load_error = ""
        try:
            self.nlp = spacy.load(model, disable=["ner"])
            self.mode = "full"
        except (OSError, ImportError) as exc:
            self.load_error = str(exc).splitlines()[0]
            print(f"[nlp] model '{model}' unavailable, using basic mode: {self.load_error}", file=sys.stderr)
            self.nlp = spacy.blank("en")
        if "sentencizer" not in self.nlp.pipe_names and "parser" not in self.nlp.pipe_names:
            self.nlp.add_pipe("sentencizer")
        self._load_skills()

    # -- setup ---------------------------------------------------------------
    def _load_skills(self) -> None:
        with (_DATA / "skills.json").open("r", encoding="utf-8") as fh:
            data = json.load(fh)
        self.skill_category: dict[str, str] = {}
        self.canonical: dict[str, str] = {}
        for category, items in data["categories"].items():
            for item in items:
                self.skill_category[item] = category
                self.canonical[item.lower()] = item
        for alias, target in data.get("aliases", {}).items():
            self.canonical[alias.lower()] = target
        self.matcher = PhraseMatcher(self.nlp.vocab, attr="LOWER")
        patterns = [self.nlp.make_doc(p) for p in self.canonical]
        self.matcher.add("SKILL", patterns)

    def status(self) -> dict:
        return {
            "model": self.model_name,
            "mode": self.mode,
            "spacy_version": spacy.__version__,
            "pipes": list(self.nlp.pipe_names),
            "skills_known": len(self.skill_category),
            "error": self.load_error,
            "install_hint": "" if self.mode == "full" else f"python -m spacy download {self.model_name}",
        }

    # -- primitives ------------------------------------------------------------
    @lru_cache(maxsize=512)
    def doc(self, text: str):
        return self.nlp(text or "")

    def _lemma(self, token) -> str:
        if self.mode == "full" and token.lemma_:
            return token.lemma_.lower()
        word = token.lower_
        # Crude plural folding for basic mode so "forklifts" matches "forklift".
        if len(word) > 4 and word.endswith("s") and not word.endswith("ss"):
            return word[:-1]
        return word

    def _is_content(self, token) -> bool:
        if token.is_stop or token.is_punct or token.is_space or token.like_num:
            return False
        if self.mode == "full":
            return token.pos_ in {"NOUN", "PROPN", "ADJ", "VERB"} and len(token.text) > 1
        return bool(_WORD.fullmatch(token.text)) and len(token.text) > 2

    def key(self, phrase: str) -> str:
        """Normalized comparison key: lowercase content lemmas joined by spaces."""
        doc = self.doc(phrase)
        parts = [self._lemma(t) for t in doc if not (t.is_punct or t.is_space or t.is_stop)]
        return " ".join(parts) or phrase.lower().strip()

    def sentences(self, text: str) -> list[str]:
        return [s.text.strip() for s in self.doc(text).sents if s.text.strip()]

    # -- skills ----------------------------------------------------------------
    def find_skills(self, text: str) -> Counter:
        doc = self.doc(text)
        found: Counter = Counter()
        spans = spacy.util.filter_spans([doc[s:e] for _, s, e in self.matcher(doc)])  # longest match wins
        for span in spans:
            canonical = self.canonical.get(span.text.lower(), span.text)
            found[canonical] += 1
        return found

    def category_of(self, skill: str) -> str:
        return self.skill_category.get(skill, "")

    # -- keyword extraction ---------------------------------------------------
    def _candidate_phrases(self, sent) -> Iterable[str]:
        if self.mode == "full":
            for chunk in sent.noun_chunks:
                tokens = [t for t in chunk if not (t.is_stop or t.is_punct or t.like_num or t.pos_ == "DET")]
                while tokens and tokens[0].lower_ in GENERIC_MODIFIERS:
                    tokens = tokens[1:]
                if 1 <= len(tokens) <= 4:
                    yield " ".join(t.text for t in tokens)
            # Verbs-as-skills ("weld", "cook") are covered by the lexicon; proper
            # nouns outside chunks (tools, certifications) are caught here.
            for token in sent:
                if token.pos_ == "PROPN" and not token.is_stop and len(token.text) > 1:
                    yield token.text
        else:
            words = [t for t in sent if self._is_content(t)]
            for t in words:
                yield t.text
            for a, b in zip(words, words[1:]):
                if b.i == a.i + 1:
                    yield f"{a.text} {b.text}"

    def keywords(self, text: str, top: int = 30) -> list[Keyword]:
        """Rank what a posting (or resume) is really asking for.

        Known skills from the lexicon always outrank free-text phrases; phrases
        found in 'must have' sentences get a boost, 'nice to have' a smaller one.
        """
        doc = self.doc(text)
        found: dict[str, Keyword] = {}
        # Postings are bullet lists without full stops; parse line by line so a
        # "Must have…" cue only boosts its own line.
        units = [s for line in text.splitlines() if line.strip() for s in self.doc(line.strip()).sents]

        def add(term: str, kind: str, weight: float, required: bool) -> None:
            k = self.key(term)
            if not k or k in BOILERPLATE or len(k) < 2 or k.isdigit():
                return
            if kind == "phrase" and all(w in BOILERPLATE for w in k.split()):
                return
            existing = found.get(k)
            if existing:
                existing.score += weight
                existing.count += 1
                existing.required = existing.required or required
                if kind == "skill" and existing.kind != "skill":
                    existing.kind, existing.term = "skill", term
                    existing.category = self.category_of(term)
            else:
                found[k] = Keyword(term=term, key=k, score=weight, kind=kind,
                                   category=self.category_of(term) if kind == "skill" else "",
                                   required=required)

        for sent in units:
            lowered = sent.text.lower()
            required = any(cue in lowered for cue in REQUIRED_CUES)
            preferred = any(cue in lowered for cue in PREFERRED_CUES)
            boost = 1.6 if required else (1.25 if preferred else 1.0)
            sent_skills = self.find_skills(sent.text)
            skill_keys = set()
            for skill, count in sent_skills.items():
                for _ in range(count):
                    add(skill, "skill", 3.0 * boost, required)
                skill_keys.add(self.key(skill))
            for phrase in self._candidate_phrases(sent):
                k = self.key(phrase)
                # Skip fragments already represented by a matched skill.
                if any(k == sk or k in sk.split() for sk in skill_keys):
                    continue
                add(phrase, "phrase", 1.0 * boost, required)

        ranked = sorted(found.values(), key=lambda kw: (kw.kind != "skill", -kw.score, kw.term.lower()))
        # Phrases seen once in a long posting are usually noise.
        long_text = len(doc) > 250
        ranked = [kw for kw in ranked if kw.kind == "skill" or kw.count > 1 or not long_text or kw.required]
        return ranked[:top]

    # -- matching support -------------------------------------------------------
    def profile_text(self, text: str) -> TextProfile:
        doc = self.doc(text)
        prof = TextProfile()
        prof.lemmas = {self._lemma(t) for t in doc if self._is_content(t)}
        prof.stems = {stem(w) for w in prof.lemmas} | {stem(t.lower_) for t in doc if self._is_content(t)}
        prof.skills = {self.key(s) for s in self.find_skills(text)}
        prof.phrases = set(prof.skills)
        for sent in doc.sents:
            for phrase in self._candidate_phrases(sent):
                prof.phrases.add(self.key(phrase))
        return prof

    def covers(self, prof: TextProfile, keyword_key: str) -> bool:
        if keyword_key in prof.phrases or keyword_key in prof.skills:
            return True
        words = [w for w in keyword_key.split() if w not in BOILERPLATE]
        return bool(words) and all(w in prof.lemmas or stem(w) in prof.stems for w in words)

    # -- bullet coaching ---------------------------------------------------------
    def review_bullet(self, text: str) -> dict:
        clean = text.strip().lstrip("-•*·").strip()
        doc = self.doc(clean)
        issues: list[str] = []
        lowered = clean.lower()
        for phrase, tip in WEAK_OPENERS.items():
            if re.search(rf"\b{re.escape(phrase)}\b", lowered):
                issues.append(tip)
        first = next((t for t in doc if not t.is_punct and not t.is_space), None)
        starts_with_verb = False
        if first is not None:
            if first.text.capitalize() in STRONG_VERBS:
                starts_with_verb = True
            elif self.mode == "full":
                # Taggers read a capitalised fragment opener as a proper noun;
                # parsing it as "I <bullet>" gives the verb its real context.
                probe = self.doc("I " + first.text.lower() + clean[len(first.text):])
                starts_with_verb = len(probe) > 1 and probe[1].pos_ in {"VERB", "AUX"} and probe[1].lower_ not in {"am", "was", "have"}
            else:
                starts_with_verb = first.lower_.endswith("ed")
        if first is not None and first.lower_ in {"i", "my", "we"}:
            issues.append("Drop 'I'/'my' — resume bullets start with the action.")
        elif not starts_with_verb:
            issues.append("Open with an action verb (e.g. " + ", ".join(STRONG_VERBS[:4]) + ").")
        has_number = bool(_NUMBER.search(clean))
        if not has_number:
            issues.append("Add a number if you can: how many, how often, how much, how fast.")
        words = len([t for t in doc if not t.is_punct])
        if words > 32:
            issues.append("Long bullet — aim for one line (under ~25 words).")
        elif words < 4:
            issues.append("Very short — add what you did and the result.")
        score = 100 - 25 * len(issues)
        return {
            "text": clean, "starts_with_verb": starts_with_verb, "has_number": has_number,
            "words": words, "issues": issues, "score": max(score, 0),
        }


_INSTANCE: NLP | None = None


def get_nlp() -> NLP:
    global _INSTANCE
    if _INSTANCE is None:
        _INSTANCE = NLP()
    return _INSTANCE
