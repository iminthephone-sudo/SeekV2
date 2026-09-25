"""spaCy writing help: summary drafts and job-duty rewrites."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import SeekService  # noqa: E402
from seek.engines.nlp import get_nlp  # noqa: E402
from seek.engines.writing import past_tense  # noqa: E402
from seek.storage import Store  # noqa: E402
from tests.fixtures import JOB_TEXT, SAMPLE_PROFILE  # noqa: E402


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


def rewrite(service, text, past=True):
    return service.call("assist.rewrite_bullet", {"text": text, "past": past})["rewrite"]


def test_past_tense_rules():
    assert [past_tense(v) for v in ("stop", "prep", "stage", "carry", "control", "lead", "open", "fix", "sweep")] == \
        ["stopped", "prepped", "staged", "carried", "controlled", "led", "opened", "fixed", "swept"]


@pytest.mark.parametrize("before,past,after", [
    ("Responsible for cleaning the kitchen and dining room", True, "Cleaned the kitchen and dining room"),
    ("Responsible for inventory", True, "Managed inventory"),
    ("I cleaned floors and restrooms", True, "Cleaned [#] floors and restrooms"),
    ("Load and unload trucks", True, "Loaded and unloaded [#] trucks"),
    ("Picks and packs orders", False, "Pick and pack [#] orders"),
    ("Duties included stocking shelves, cleaning, and helping customers", True,
     "Stocked shelves, cleaned, and helped [#] customers"),
    ("Make sure all orders were correct", True, "Ensured all orders were correct"),
    ("Served food and drinks", True, "Served food and drinks"),  # "drinks" is a noun here, not a verb
    ("Prepared 400 meals daily", True, "Prepared 400 meals daily"),  # already has a number
    ("Staged outbound shipments", False, "Stage [#] outbound shipments"),  # not "Stag"
])
def test_rewrite_bullet(service, before, past, after):
    assert rewrite(service, before, past) == after


def test_rewrite_flags_setting_words(service):
    result = service.call("assist.rewrite_bullet", {"text": "Cooked meals for inmates"})
    assert result["warnings"]


def test_duties_suggests_for_title_and_posting_without_repeats(service):
    job = service.call("job.from_text", {"text": JOB_TEXT, "title": "Forklift Operator", "company": "FastShip"})
    out = service.call("assist.duties", {"title": "Order Picker", "current": True, "job_id": job["id"],
                                         "bullets": ["Picked and packed 120 orders per shift using an RF scanner"]})
    assert out["occupation"] == "Warehouse Associate" and out["tense"] == "present"
    texts = [s["text"] for s in out["suggestions"]]
    assert not any(t.startswith("Pick and pack [#] orders") for t in texts)  # already on the resume
    assert any(s["source"].startswith("Posting:") for s in out["suggestions"])
    assert all("[#]" not in t for t in texts if t.startswith("Operate forklift"))  # posting lines get no prompts


def test_generic_titles_do_not_match_an_occupation(service):
    assert service.writing.match_occupation("Crew Member") is None
    assert service.writing.match_occupation("Software Engineer") is None
    assert service.writing.match_occupation("Forklift driver")["title"] == "Forklift Operator"


def test_summary_drafts_use_profile_facts_and_posting(service):
    profile = service.call("profile.create", {"name": "W", "data": SAMPLE_PROFILE})
    job = service.call("job.from_text", {"text": JOB_TEXT, "title": "Forklift Operator", "company": "FastShip"})
    out = service.call("assist.summary", {"profile_id": profile["id"], "job_id": job["id"]})
    assert out["facts"]["role"] == "Forklift Operator"
    assert out["facts"]["months"] >= 24
    labels = [d["label"] for d in out["drafts"]]
    assert "Aimed at this posting" in labels
    for d in out["drafts"]:
        assert "Correctional" not in d["text"]  # never the facility
        assert d["text"][0].isupper() and ". ready" not in d["text"]
        lowered = d["text"].lower()
        assert lowered.count("forklift,") <= 1  # no duplicated skills


def test_summary_review_flags_first_person_cliches_and_setting(service):
    profile = service.call("profile.create", {"name": "W", "data": SAMPLE_PROFILE})
    out = service.call("assist.summary", {"profile_id": profile["id"],
                                          "text": "I am a hard worker and team player who was in prison."})
    kinds = {r["kind"] for r in out["review"]}
    assert {"first_person", "cliche", "setting", "short"} <= kinds


def test_resume_analysis_catches_leftover_placeholders(service):
    data = dict(SAMPLE_PROFILE)
    data["experience"] = [dict(SAMPLE_PROFILE["experience"][1], bullets=["Loaded [#] trucks per shift"])]
    profile = service.call("profile.create", {"name": "W", "data": data})
    result = service.call("resume.analyze", {"profile_id": profile["id"]})
    assert not next(c for c in result["checks"] if c["label"] == "No [#] left")["ok"]
    assert any("[#]" in i for b in result["bullets"] for i in b["issues"])
