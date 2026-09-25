"""spaCy writing help for the two hardest resume sections: the summary and job duties.

*Summary* — drafts built only from facts already in the profile (titles, time
worked, skills, certificates, assessment strengths) and, when a posting is
picked, the posting's own wording for skills the participant really has. The
current summary is reviewed too (length, first person, clichés, setting words).

*Duties* — each bullet is parsed with spaCy and rewritten: weak openers
("Responsible for…", "Helped with…") become action verbs, the verb is put in the
right tense (past for old jobs, present for the current one), "I" is dropped,
and a ``[#]`` prompt is put in front of the thing that can be counted. Typical
duties for the job title (and the posting's duties, rewritten as bullets) are
offered as suggestions. Nothing is changed until staff choose it.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from ..storage import NotFound, Store, as_list
from .fair_chance import _scan as scan_sensitive
from .fair_chance import parse_when
from .nlp import NLP, article, needs_exact, stem
from .profiles import ProfileEngine, normalize, profile_text

_DATA = Path(__file__).resolve().parent.parent / "data"
PLACEHOLDER = "[#]"

IRREGULAR_PAST = {
    "be": "was", "bend": "bent", "bring": "brought", "build": "built", "buy": "bought", "catch": "caught", "cut": "cut",
    "deal": "dealt", "dig": "dug", "do": "did", "drive": "drove", "feed": "fed", "feel": "felt", "fight": "fought",
    "find": "found", "freeze": "froze", "get": "got", "give": "gave", "go": "went", "grow": "grew", "hang": "hung",
    "have": "had", "hit": "hit", "hold": "held", "keep": "kept", "know": "knew", "lay": "laid", "lead": "led",
    "leave": "left", "lend": "lent", "light": "lit", "lose": "lost", "make": "made", "mean": "meant", "meet": "met",
    "oversee": "oversaw", "pay": "paid", "put": "put", "read": "read", "rebuild": "rebuilt", "ride": "rode",
    "run": "ran", "say": "said", "see": "saw", "sell": "sold", "send": "sent", "set": "set", "shake": "shook",
    "shut": "shut", "sit": "sat", "sleep": "slept", "speak": "spoke", "spend": "spent", "spin": "spun",
    "split": "split", "stand": "stood", "stick": "stuck", "strike": "struck", "sweep": "swept", "swing": "swung",
    "take": "took", "teach": "taught", "tear": "tore", "think": "thought", "throw": "threw",
    "understand": "understood", "undertake": "undertook", "wake": "woke", "wear": "wore", "win": "won",
    "wind": "wound", "write": "wrote", "rewrite": "rewrote", "break": "broke", "tell": "told", "begin": "began",
    "choose": "chose", "draw": "drew", "hear": "heard", "let": "let", "quit": "quit", "seek": "sought",
    "become": "became", "fall": "fell", "spread": "spread", "shoot": "shot", "forget": "forgot", "ring": "rang",
    "sing": "sang", "drink": "drank", "eat": "ate", "fly": "flew", "forgive": "forgave", "hide": "hid", "rise": "rose",
    "slide": "slid", "steal": "stole", "sting": "stung", "swim": "swam", "bite": "bit", "blow": "blew", "hurt": "hurt",
    "cast": "cast", "cost": "cost", "bet": "bet", "burst": "burst", "overcome": "overcame", "undergo": "underwent",
    "uphold": "upheld", "withstand": "withstood", "broadcast": "broadcast", "shed": "shed", "bleed": "bled",
    "breed": "bred", "flee": "fled", "speed": "sped", "cling": "clung", "string": "strung", "sink": "sank",
    "shrink": "shrank", "grind": "ground", "weave": "wove", "stride": "strode", "arise": "arose", "awake": "awoke",
    "forecast": "forecast", "mislead": "misled", "outdo": "outdid", "overhear": "overheard", "oversee": "oversaw",
    "overtake": "overtook", "override": "overrode", "retake": "retook", "rerun": "reran", "resell": "resold",
    "retell": "retold", "rethink": "rethought", "upset": "upset", "input": "input", "output": "output",
}
PAST_TO_BASE = {v: k for k, v in IRREGULAR_PAST.items() if v != k}
# Same spelling in both tenses ("Read blueprints…"): the tense of the words listed after them can't be inferred.
SAME_FORMS = {k for k, v in IRREGULAR_PAST.items() if k == v}
# -ing words that are almost always nouns after "responsible for" ("building maintenance", "morning prep").
ING_NOUNS = {"building", "morning", "evening", "ceiling", "housing", "spring", "clothing", "dining", "parking",
             "flooring", "roofing", "siding", "lighting", "plumbing", "wiring", "bedding", "lodging", "heating",
             "awning", "boarding", "catering", "landscaping", "shipping", "receiving", "nothing", "something",
             "everything", "anything", "thing", "king", "string", "ring", "wing", "sibling", "offspring", "pudding",
             "icing", "stuffing", "seasoning", "dressing", "frosting", "filling", "topping", "coating"}
# Two-syllable verbs stressed on the last syllable double the final consonant.
DOUBLE_FINAL = {"control", "patrol", "equip", "refer", "transfer", "commit", "submit", "admit", "permit", "occur",
                "prefer", "compel", "expel", "propel", "regret"}

# Openers that hide the action. Value: verb to use when a noun follows (a gerund keeps its own verb).
WEAK_OPENERS = [
    (r"(?:my )?(?:job )?duties (?:were|included)(?: to)?", "Performed"),
    (r"(?:i was |was )?responsible for", "Managed"),
    (r"(?:i was |was )?in charge of", "Led"),
    (r"(?:i was |was )?tasked with", "Handled"),
    (r"(?:i was |was )?involved (?:in|with)", "Contributed to"),
    (r"(?:i )?helped (?:with|to)?", "Helped"),
    (r"(?:i )?assisted (?:with|in)", "Assisted with"),
]
# Vague verb phrases -> precise ones (base forms).
UPGRADES = [
    (r"\bmake sure\b", "ensure"), (r"\bmade sure\b", "ensured"), (r"\bmaking sure\b", "ensuring"),
    (r"^take care of\b", "maintain"), (r"^took care of\b", "maintained"),
    (r"^deal with\b", "handle"), (r"^dealt with\b", "handled"),
    (r"^talk to\b", "communicate with"), (r"^talked to\b", "communicated with"),
    (r"^do\b", "complete"), (r"^did\b", "completed"),
]
GENERIC_TITLE_WORDS = {stem(w) for w in ("crew", "worker", "associate", "helper", "member", "lead", "assistant",
                                         "operator", "technician", "specialist", "general", "senior", "junior",
                                         "trainee", "apprentice", "team", "staff", "aide", "attendant")}
CLICHES = ["hard worker", "hard-working", "hardworking", "team player", "go-getter", "people person",
           "detail oriented", "detail-oriented", "self-starter", "think outside the box", "results-driven",
           "results oriented", "dynamic", "synergy", "motivated individual", "fast learner", "quick learner"]
_NUMBER = re.compile(r"\d|\b(one|two|three|four|five|six|seven|eight|nine|ten|dozen|hundred|thousand)\b", re.I)


def past_tense(verb: str) -> str:
    """Regular and irregular English past tense for a base-form verb."""
    v = verb.lower()
    if v in IRREGULAR_PAST:
        return IRREGULAR_PAST[v]
    if v.endswith("e"):
        return v + "d"
    if len(v) > 2 and v.endswith("y") and v[-2] not in "aeiou":
        return v[:-1] + "ied"
    if v in DOUBLE_FINAL or (len(re.findall(r"[aeiou]+", v)) == 1 and re.search(r"[^aeiou][aeiou][^aeiouwxy]$", v)):
        return v + v[-1] + "ed"
    return v + "ed"


def _cap(text: str) -> str:
    return text[:1].upper() + text[1:] if text else text


def _mid(term: str) -> str:
    """Skill as it reads mid-sentence: "Inventory" -> "inventory", but "ServSafe", "RF scanner", "OSHA 10" stay."""
    first = term.split(" ")[0]
    return term[:1].lower() + term[1:] if first[:1].isupper() and first[1:].islower() else term


def _join(items: list[str]) -> str:
    items = [i for i in items if i]
    if len(items) <= 1:
        return "".join(items)
    return ", ".join(items[:-1]) + " and " + items[-1]


class WritingAssist:
    def __init__(self, store: Store, nlp: NLP, profiles: ProfileEngine) -> None:
        self.store = store
        self.nlp = nlp
        self.profiles = profiles
        with (_DATA / "duties.json").open("r", encoding="utf-8") as fh:
            self.occupations = json.load(fh)["occupations"]

    # -- bullets -------------------------------------------------------------------------------
    def _verb_form(self, lemma: str, past: bool) -> str:
        return past_tense(lemma) if past else lemma

    def _retense(self, text: str, past: bool) -> tuple[str, bool, int]:
        """Put the opening verb (and verbs joined to it) in the right tense.

        Returns (text, changed, verb_end): verb_end is how many words the opening verbs span, so the [#]
        prompt can be placed after them.

        The bullet is parsed as a clause — "I <bullet>", or "I was <bullet>" when it opens with a gerund —
        so the tagger sees verbs, not a title. The sentence is rebuilt from the tokens, so offsets never drift.
        """
        if self.nlp.mode != "full" or not text:
            return text, False, 0
        first_word = text.split()[0].lower()
        prefix = "I was " if first_word.endswith("ing") else "I "
        probe = self.nlp.doc(prefix + text[:1].lower() + text[1:])
        skip = len(self.nlp.doc(prefix.strip()))
        if len(probe) <= skip:
            return text, False, 0
        head = probe[skip]
        head_lemma = head.lemma_.lower()
        if head.pos_ not in ("VERB", "AUX"):
            # "Tutor learners…": tagged as a noun, but it works as a verb and is followed by its object.
            nxt = probe[head.i + 1] if head.i + 1 < len(probe) else None
            head_lemma = self._verb_lemma(head.text) if nxt is not None and nxt.pos_ in ("NOUN", "DET", "PRON", "ADJ") else ""
            if not head_lemma or head.text.isupper():
                return text, False, 0
        if head_lemma in ("be", "have"):
            return text, False, 0
        verbs = {head.i: head_lemma}
        for c in head.conjuncts:
            # Same verb form as the opener only: in "Installed framing and trim", "framing" isn't a verb.
            if head.lower_ in SAME_FORMS:
                break  # "Read … and weld …": the tense of the list can't be told, so leave it as written
            if c.pos_ == "VERB" and c.i > head.i and (c.tag_ == head.tag_ or {c.tag_, head.tag_} <= {"VB", "VBP"}):
                verbs[c.i] = c.lemma_.lower()
        # The small model often tags a listed verb as a noun ("load and unload trucks", "stocking shelves,
        # cleaning, and helping"). A word right after a list separator counts when it has the same form as the
        # opening verb and really is a verb.
        gerund = head.tag_ == "VBG"
        for tok in ([] if head.lower_ in SAME_FORMS else probe[head.i + 1:]):
            if tok.i in verbs or tok.i == 0 or probe[tok.i - 1].text not in (",", "and", "or"):
                continue
            sep = probe[tok.i - 1]
            if gerund:
                # "…and dining room": an -ing word describing the next noun is not a verb.
                describes_next = tok.dep_ in ("amod", "compound") and tok.head.i == tok.i + 1
                if tok.lower_.endswith("ing") and not describes_next:
                    lemma = self._verb_lemma(tok.text)
                    if lemma:
                        verbs[tok.i] = lemma
            elif sep.text in ("and", "or"):
                nxt = probe[tok.i + 1] if tok.i + 1 < len(probe) else None
                # "…and unload trucks": joined to the verb, or followed by its own object.
                attached = sep.head.i == head.i or (nxt is not None and (nxt.pos_ in ("NOUN", "PROPN", "DET", "NUM", "ADJ")
                                                                          or nxt.tag_ in ("VBG", "NN", "NNS")))
                lemma = self._base(tok, self._verb_lemma(tok.text)) if attached else ""
                if lemma and self._same_form(tok.lower_, lemma, head):
                    verbs[tok.i] = lemma
        out, changed = [], False
        for tok in probe[skip:]:
            word = tok.text
            if tok.i in verbs and not self._already(tok, past):
                base = self._base(tok, verbs[tok.i])
                want = self._verb_form(base, past) if base else ""
                if want and want != tok.lower_:  # no reliable base form: leave the word as it was written
                    word, changed = want, True
            out.append(word + tok.whitespace_)
        rebuilt = "".join(out).strip()
        # "Helped load trucks": the verb run includes an infinitive after the opener.
        last = max([*verbs, *(c.i for c in head.children if c.dep_ == "xcomp" and c.pos_ == "VERB")])
        return _cap(rebuilt), changed, last - skip + 1

    @staticmethod
    def _already(tok, past: bool) -> bool:
        """Is the verb already in the tense we want? Then it's never touched ("Broke down" stays)."""
        w = tok.lower_
        if past:
            return tok.tag_ in ("VBD", "VBN") or w in PAST_TO_BASE or (w.endswith("ed") and tok.tag_ != "VBG")
        return tok.tag_ in ("VB", "VBP") and w not in PAST_TO_BASE and not w.endswith("ed")

    def _base(self, tok, lemma: str) -> str:
        """A base form we trust for ``tok``, or "" when the lemmatizer's guess can't be verified."""
        w = tok.lower_
        lemma = (lemma or "").lower()
        if tok.text.isupper() and len(tok.text) > 1:
            return ""  # acronyms ("GED", "OSHA") are never verbs
        if w in PAST_TO_BASE:
            return PAST_TO_BASE[w]
        if w in IRREGULAR_PAST:
            return w
        if w.endswith("ed"):  # verify by inflecting back: "staged" -> "stage" (not "stag"), "installed" -> "install"
            cands = ([w[:-3] + "y"] if w.endswith("ied") else []) + [lemma, w[:-2], w[:-1], w[:-3]]
            return next((c for c in cands if c and past_tense(c) == w and self._verb_lemma(c)
                         and self._verb_lemma(c) in (c, lemma)), "")
        if w.endswith("ing"):
            cands = [lemma, w[:-3], w[:-3] + "e", w[:-4]]
            return next((c for c in cands if c and len(c) > 1 and self._verb_lemma(c) == c
                         and (c + "ing" == w or c[:-1] + "ing" == w or c + c[-1] + "ing" == w)), "")
        if w.endswith("s") and tok.tag_ == "VBZ":
            cands = ([w[:-3] + "y"] if w.endswith("ies") else []) + ([w[:-2]] if w.endswith("es") else []) + [w[:-1]]
            return next((c for c in cands if c and self._verb_lemma(c) == c), "")
        return w if self._verb_lemma(w) else ""  # a base form written as is ("Ring up", "Load")

    @staticmethod
    def _same_form(word: str, lemma: str, head) -> bool:
        """Does ``word`` have the same verb form as the opening verb (base, -s or past)?"""
        if head.tag_ == "VBZ":
            return word in (lemma + "s", lemma + "es", lemma[:-1] + "ies")
        if head.tag_ == "VBD":
            return word == past_tense(lemma)
        return word == lemma

    def _verb_lemma(self, word: str) -> str:
        """Base form of ``word`` if it works as a verb in a clause ("unload", "cleaning" -> "clean"), else ""."""
        w = word.lower()
        probe = self.nlp.doc(("I am " if w.endswith("ing") else "I ") + w + " it")
        tok = probe[2] if w.endswith("ing") else probe[1]
        return tok.lemma_.lower() if tok.pos_ == "VERB" else ""

    def _add_placeholder(self, text: str, verb_end: int = 1) -> tuple[str, bool]:
        """Put [#] in front of the first countable object: "Served customers" -> "Served [#] customers"."""
        if self.nlp.mode != "full" or _NUMBER.search(text) or PLACEHOLDER in text:
            return text, False
        prefix = "I "
        probe = self.nlp.doc(prefix + text[:1].lower() + text[1:])
        for tok in probe[2:]:
            if tok.tag_ != "NNS" or tok.dep_ not in ("dobj", "conj"):
                continue
            if tok.dep_ == "conj" and tok.head.pos_ not in ("VERB", "AUX"):
                continue  # "food and drinks": the second of two objects isn't the one to count
            if any(c.dep_ == "nummod" for c in tok.children):
                return text, False
            # Start of the noun phrase: determiners, adjectives and compounds, never a verb.
            left = tok
            for c in tok.lefts:
                if (c.dep_ in ("det", "amod", "compound", "poss") and c.pos_ not in ("VERB", "AUX")
                        and not c.tag_.startswith("VB") and c.i < left.i):
                    left = c
            # Never before the opening verbs (the tagger sometimes files "…and unloaded" under the noun).
            if left.i < 1 + verb_end:
                if tok.i < 1 + verb_end:
                    continue
                left = probe[1 + verb_end]
            at = left.idx - len(prefix)
            if left.lower_ in ("the", "all", "many", "several", "multiple", "various", "numerous", "some"):
                return text[:at] + PLACEHOLDER + text[at + len(left.text):], True
            return text[:at] + PLACEHOLDER + " " + text[at:], True
        return text, False

    def rewrite_bullet(self, text: str, past: bool = True, placeholder: bool = True) -> dict[str, Any]:
        """Rewrite one duty bullet. Returns the rewrite and what changed (in plain words)."""
        original = text
        clean = re.sub(r"\s+", " ", str(text).strip().lstrip("-•*·").strip()).rstrip(".;")
        changes: list[str] = []
        if not clean:
            return {"original": original, "rewrite": "", "changes": [], "needs_number": False, "warnings": []}

        # "I cleaned…" / "We loaded…": resume bullets start with the action.
        m = re.match(r"^(i|we)\s+(?!was\b|were\b|am\b)", clean, re.I)
        if m:
            clean = _cap(clean[m.end():])
            changes.append("Dropped “I” — bullets start with the action.")

        for pattern, verb in WEAK_OPENERS:
            m = re.match(rf"^{pattern}\s+(.*)$", clean, re.I)
            if not m:
                continue
            rest = m.group(1)
            first = rest.split()[0].lower()
            gerund = first.endswith("ing") and first not in ING_NOUNS and bool(self._verb_lemma(first))
            if verb == "Helped" and not gerund and self._verb_lemma(first) == first:
                clean = "Helped " + rest  # "Helped to load trucks" -> "Helped load trucks"
            elif verb == "Helped" and not gerund:
                clean = "Assisted with " + rest  # "Helped with spring cleaning"
            elif gerund:
                # "Responsible for cleaning the kitchen" -> "Cleaning the kitchen" -> (tense) "Cleaned the kitchen"
                if verb == "Helped":  # "Helped with loading trucks" -> "Helped load trucks"
                    words = rest.split(" ", 1)
                    clean = "Helped " + (self._verb_lemma(words[0]) or words[0]) + (" " + words[1] if len(words) > 1 else "")
                else:
                    clean = _cap(rest)
            else:
                clean = f"{verb} {rest}"
            changes.append("Replaced a weak opener with an action verb.")
            break

        for pattern, repl in UPGRADES:
            new = re.sub(pattern, repl, clean, count=1, flags=re.I)
            if new != clean:
                changes.append(f"Made the verb more precise (“{repl}”).")
                clean = _cap(new)
                break

        clean, changed, verb_end = self._retense(clean, past)
        if changed:
            changes.append("Put the verb in the " + ("past tense for a past job." if past else "present tense for a current job."))
        clean, added = self._add_placeholder(clean, max(verb_end, 1)) if placeholder else (clean, False)
        if added:
            changes.append(f"Added {PLACEHOLDER} where a number would help — fill it in or delete it.")
        clean = _cap(re.sub(r"\s+", " ", clean).strip())
        warnings = [f"“{term}” names the setting; describe the work and who was served instead."
                    for term, _ in scan_sensitive(clean)[:1]]
        return {"original": original, "rewrite": clean, "changes": changes, "warnings": warnings,
                "needs_number": PLACEHOLDER in clean or not _NUMBER.search(clean),
                "review": self.nlp.review_bullet(clean)}

    # -- occupations ---------------------------------------------------------------------------
    def _words(self, text: str) -> set[str]:
        return {stem(w) for w in self.nlp.key(text).split() if len(w) > 1}

    def _title_words(self, text: str) -> set[str]:
        """Words that say what the job is: "Kitchen Crew Lead" -> {kitchen}. Generic role words don't count."""
        return self._words(text) - GENERIC_TITLE_WORDS

    def match_occupation(self, title: str) -> dict[str, Any] | None:
        words = self._title_words(title)
        if not words:
            return None
        best, best_score = None, 0.0
        for occ in self.occupations:
            for name in [occ["title"], *occ.get("aliases", [])]:
                name_words = self._title_words(name)
                if not name_words:
                    continue
                overlap = len(words & name_words)
                # Cover most of the occupation name, and prefer exact-length matches.
                score = overlap / len(name_words) + 0.1 * overlap / len(words)
                if overlap and score > best_score:
                    best, best_score = occ, score
        return best if best_score >= 0.5 else None

    def duties(self, title: str = "", bullets: list[str] | None = None, current: bool = False, job_id: str = "",
               employer: str = "") -> dict[str, Any]:
        bullets = as_list(bullets)
        past = not current
        rewrites = [self.rewrite_bullet(b, past=past) for b in bullets]
        have = [self._words(b) for b in bullets]

        def already_have(text: str) -> bool:
            w = self._words(text)
            return any(w and len(w & h) / len(w) >= 0.6 for h in have)

        suggestions: list[dict[str, str]] = []
        occ = self.match_occupation(title) if title else None
        if occ:
            for d in occ["duties"]:
                text = d if past else self.rewrite_bullet(d, past=False, placeholder=False)["rewrite"]
                if not already_have(text):
                    suggestions.append({"text": text, "source": occ["title"]})
        if job_id:
            job = self.store.get("jobs", job_id)
            if job:
                lines = job.get("sections", {}).get("responsibilities", [])[:10]
                for line in lines:
                    rw = self.rewrite_bullet(line, past=past, placeholder=False)["rewrite"]
                    if rw and len(rw.split()) >= 3 and not already_have(rw):
                        suggestions.append({"text": rw, "source": f"Posting: {job.get('title', '')}"})
        return {"occupation": occ["title"] if occ else "", "tense": "past" if past else "present",
                "bullets": rewrites, "suggestions": suggestions,
                "note": "Only keep duties the participant really did. Replace every [#] with a real number or delete it."}

    # -- summary ---------------------------------------------------------------------------------
    def _months_worked(self, profile: dict[str, Any]) -> int:
        """Months of work, overlaps merged. A year with no month ("2019") is read as mid-year, so "2019–2020"
        counts as 12 months: a summary must never claim more time than the participant may have worked."""
        year_only = re.compile(r"^\s*\d{4}\s*$")
        spans = []
        for x in profile.get("experience", []):
            raw_start, raw_end = str(x.get("start", "") or ""), str(x.get("end", "") or "")
            start = parse_when(raw_start)
            end = parse_when(raw_end or "present", is_end=True)
            if not (start and end):
                continue
            if year_only.match(raw_start):
                start = start.replace(month=7)
            if year_only.match(raw_end):
                end = end.replace(month=6)
            spans.append((start, max(start, end)))
        spans.sort()
        months, cur_start, cur_end = 0, None, None
        for s_, e in spans:  # merge overlapping jobs so they aren't counted twice
            if cur_end and s_ <= cur_end:
                cur_end = max(cur_end, e)
                continue
            if cur_start:
                months += (cur_end.year - cur_start.year) * 12 + cur_end.month - cur_start.month + 1
            cur_start, cur_end = s_, e
        if cur_start:
            months += (cur_end.year - cur_start.year) * 12 + cur_end.month - cur_start.month + 1
        return months

    def _strengths(self, profile_id: str) -> list[str]:
        """Strengths from the participant's latest saved workplace-assessment practice."""
        rows = [r for r in self.store.all("interview") if r.get("profile_id") == profile_id and r.get("type") == "assessment"]
        if not rows:
            return []
        latest = max(rows, key=lambda r: r.get("created_at", ""))
        return [t["label"].lower() for t in latest.get("traits", []) if t.get("score", 0) >= 75][:2]

    def summary_facts(self, profile: dict[str, Any], job: dict[str, Any] | None) -> dict[str, Any]:
        clean = lambda v: v if v and not scan_sensitive(v) else ""  # noqa: E731
        exp = sorted(profile.get("experience", []),
                     key=lambda x: parse_when(x.get("end", "") or "present", is_end=True) or parse_when("1900"),
                     reverse=True)  # most recent job first
        titles = [t for t in dict.fromkeys(clean(x.get("title", "")) for x in exp) if t]
        role = ""
        if job:
            from .jobs import clean_title
            role = clean_title(job.get("title", ""), job.get("company", ""))
        role = clean(role or next(iter(profile.get("target_roles", [])), "") or (titles[0] if titles else ""))

        certs = [c.get("name", "") for c in profile.get("certifications", []) if clean(c.get("name", ""))]
        cert_words = {w for c in certs for w in self.nlp.key(c).split()} - {"certification", "certificate", "card"}
        seen: set[str] = set()
        spelled: dict[str, str] = {}  # key -> the participant's own spelling

        def add_skill(term: str, out: list[str]) -> None:
            k = self.nlp.key(term)
            # Skip duplicates and skills that just restate a certificate ("forklift certification").
            if not k or k in seen or set(k.split()) <= cert_words | {"certification", "certificate", "certified"} and \
                    any(w in cert_words for w in k.split()) and "certif" in k:
                return
            seen.add(k)
            out.append(spelled.get(k, term))

        for sk in profile.get("skills", []):
            spelled.setdefault(self.nlp.key(sk), sk)
        matched: list[str] = []
        if job:
            prof = self.nlp.profile_text(profile_text(profile))
            for kw in job.get("keywords", [])[:30]:
                if self.nlp.covers(prof, kw["key"], strict=needs_exact(kw)):
                    add_skill(kw["term"], matched)
        skills = list(matched)
        for sk in profile.get("skills", []):
            add_skill(sk, skills)
        for sk, _ in self.nlp.find_skills(profile_text(profile)).most_common():
            add_skill(sk, skills)
        months = self._months_worked(profile)
        return {"role": role, "titles": titles[:3], "skills": skills[:6], "matched": matched[:6],
                "certifications": certs[:3], "months": months,
                "years_phrase": (f"{months // 12}+ years of" if months >= 24 else
                                 "over a year of" if months >= 12 else "hands-on"),
                "strengths": self._strengths(profile.get("id", "")),
                "company": clean(job.get("company", "")) if job else ""}

    def review_summary(self, text: str, facts: dict[str, Any]) -> list[dict[str, str]]:
        text = (text or "").strip()
        out: list[dict[str, str]] = []

        def note(kind: str, message: str) -> None:
            out.append({"kind": kind, "message": message})

        if not text:
            note("empty", "No summary yet. Pick a draft below as a starting point.")
            return out
        words = len(text.split())
        if words < 30:
            note("short", f"{words} words — aim for 2–3 sentences (about 35–80 words).")
        elif words > 95:
            note("long", f"{words} words — trim to 2–3 sentences; employers skim the top of the page.")
        if re.search(r"\b(i|my|me|i'm|i've)\b", text, re.I):
            note("first_person", "Resume summaries leave out “I” and “my”: “Warehouse associate with…”, not “I am a…”.")
        cliches = [c for c in CLICHES if c in text.lower()]
        if cliches:
            note("cliche", "Show it instead of saying it: " + ", ".join(f"“{c}”" for c in cliches[:3]) +
                 " — name a skill, certificate or result that proves it.")
        for term, _ in scan_sensitive(text)[:1]:
            note("setting", f"“{term}” leads with the setting. Lead with the skill and the work instead.")
        prof = self.nlp.profile_text(text)
        if facts["skills"] and not any(self.nlp.covers(prof, self.nlp.key(s)) for s in facts["skills"][:6]):
            note("skills", "Name two or three of the participant's skills, e.g. " + _join(facts["skills"][:3]) + ".")
        if facts["role"] and not self.nlp.covers(prof, self.nlp.key(facts["role"])):
            note("target", f"Say the kind of work wanted, e.g. “{facts['role']}”.")
        if not _NUMBER.search(text) and facts["months"] >= 12:
            note("number", "Add one number (years of experience, a certificate count, a result).")
        return out

    def summary(self, profile_id: str, job_id: str = "", text: str | None = None,
                data: dict[str, Any] | None = None) -> dict[str, Any]:
        """``data``: unsaved edits from the editor, laid over the saved profile (nothing is stored)."""
        profile = self.profiles.get(profile_id)
        if data:
            profile = normalize({**profile, **data, "id": profile_id})
        job = self.store.get("jobs", job_id) if job_id else None
        if job_id and job is None:
            raise NotFound(f"job not found: {job_id}")
        f = self.summary_facts(profile, job)
        current = profile.get("summary", "") if text is None else text
        role = f["role"]
        role_lower = role  # job titles keep their capitals mid-sentence ("a Forklift Operator role")
        role_key = self.nlp.key(role) if role else ""
        # "Line Cook with experience in line cook" — the role isn't also one of the skills.
        skills = [_mid(x) for x in f["skills"] if self.nlp.key(x) != role_key]
        matched = [_mid(x) for x in f["matched"] if self.nlp.key(x) != role_key]
        if not (role or skills or f["certifications"]):
            return {"facts": f, "review": self.review_summary(current, f), "drafts": [],
                    "note": "Add work experience, skills or certificates to the profile first — drafts are built only "
                            "from what's in it."}
        role = role or "Entry-level worker"
        role_lower = role
        with_exp = f" with {f['years_phrase']} experience" if f["months"] else ""
        title_phrase = lambda t: f" as {article(t)} {t}"  # noqa: E731
        certs = f["certifications"]
        strengths = f["strengths"] or []

        drafts: list[dict[str, str]] = []

        def add(label: str, sentences: list[str]) -> None:
            body = " ".join(_cap(s.strip()) for s in sentences if s and s.strip())
            body = re.sub(r"\s+", " ", body).strip()
            if body and all(body != d["text"] for d in drafts):
                drafts.append({"label": label, "text": body, "words": str(len(body.split()))})

        # 1. Experience-led.
        exp = (f"{_cap(role_lower)}{with_exp}" +
               (title_phrase(_join(f["titles"][:2])) if with_exp and f["titles"] and f["titles"][0].lower() != role.lower()
                else "") +
               ((" in " if with_exp else " skilled in ") + f"{_join(skills[:3])}." if skills else "."))
        add("Experience first", [
            exp,
            f"Holds {_join(certs)}." if certs else "",
            (f"Known for {_join(strengths)}; " if strengths else "") +
            (f"ready to bring {_join(skills[3:5])} to {f['company'] or 'a team that values steady, careful work'}."
             if skills[3:5] else f"ready to bring that experience to {f['company'] or 'a new team'}."),
        ])
        # 2. Skills-first and short.
        add("Skills first", [
            (f"{_cap(_join(skills[:4]))} — {'certified in ' + _join(certs[:2]) + ', ' if certs else ''}"
             + (f"{with_exp.strip()}{title_phrase(f['titles'][0]) if f['titles'] else ''}." if with_exp
                else "ready to learn and grow.")) if skills else "",
            f"Looking for {article(role_lower)} {role_lower} role" + (f" at {f['company']}" if f["company"] else "")
            + " to grow with.",
        ])
        # 3. Posting-targeted, when a posting is chosen and there are real matches.
        if job and matched:
            add("Aimed at this posting", [
                f"{_cap(role_lower)} candidate bringing {_join(matched[:3])}" +
                (f",{with_exp}{title_phrase(f['titles'][0])}" if with_exp and f["titles"] else "") + ".",
                f"Holds {_join(certs[:2])}." if certs else "",
                f"Ready to {('support ' + f['company']) if f['company'] else 'contribute from day one'}"
                + (f" with the {_join(strengths)} employers look for." if strengths else "."),
            ])
        return {"facts": f, "review": self.review_summary(current, f), "drafts": drafts,
                "note": "Drafts use only what's already in the profile. Edit them so they sound like the participant."}
