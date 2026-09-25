"""Coaching for interview questions about a criminal record."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import BridgeError, SeekService  # noqa: E402
from seek.engines.nlp import get_nlp  # noqa: E402
from seek.storage import Store  # noqa: E402
from tests.fixtures import SAMPLE_PROFILE  # noqa: E402

STRONG = ("Yes. In 2019 I was convicted of a theft-related offense. I made a bad decision and I take full responsibility "
          "for it. Since then I've completed a 12-week warehouse pre-apprenticeship, earned my forklift certification "
          "and worked eight months on a recycling crew without missing a shift. I'm ready to bring that same "
          "reliability to your team.")
WEAK = ("Yeah I caught a case back in 2018, it was just a little weed, basically nothing. I was at the wrong place at "
        "the wrong time and the cops set me up. My public defender didn't even try and the judge gave me 3 years. "
        "I did my bid and got out in 2021.")


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


def coach(service, answer, qid="convicted", **kw):
    return service.call("interview.record_coach", {"question_id": qid, "answer": answer, **kw})


def test_bank_has_questions_elements_and_legal_note(service):
    bank = service.call("interview.record_questions")
    assert len(bank["questions"]) >= 6
    assert [e["key"] for e in bank["elements"]] == ["acknowledge", "ownership", "change", "pivot"]
    assert "expunged" in bank["legal_note"]


def test_every_model_answer_scores_as_ready(service):
    for q in service.record_coach.questions:
        result = coach(service, q["example"], q["id"])
        assert result["score"] >= 85, (q["id"], result["issues"], result["elements"])


def test_strong_answer_covers_all_four_moves(service):
    result = coach(service, STRONG)
    assert all(e["present"] for e in result["elements"])
    assert result["issues"] == []
    assert [o["key"] for o in result["outline"]] == ["acknowledge", "ownership", "change", "pivot"]


def test_weak_answer_flags_blame_minimizing_detail_and_slang(service):
    result = coach(service, WEAK)
    kinds = {i["kind"] for i in result["issues"]}
    assert {"blame", "minimize", "overshare", "slang"} <= kinds
    assert result["score"] < 30
    slang = next(i for i in result["issues"] if i["kind"] == "slang")
    assert "was charged" in slang["message"]  # offers the plain-word swap


def test_answer_stuck_in_the_past_is_flagged(service):
    result = coach(service, "Yes, I have a felony from 2015. I was young and I made a lot of bad choices back then. "
                            "I regret it every day and I take responsibility for what I did. It was a hard time in my "
                            "life and I was not proud of who I was.")
    kinds = {i["kind"] for i in result["issues"]}
    assert "balance" in kinds
    assert next(e for e in result["elements"] if e["key"] == "change")["present"] is False


def test_everyday_words_are_not_flagged(service):
    result = coach(service, "I'd love a shot at this job, even with a trial period. My co-worker booked the "
                            "appointments and I caught up on the work.")
    kinds = {i["kind"] for i in result["issues"]}
    assert not kinds & {"overshare", "slang", "blame"}


def test_question_only_scores_the_moves_it_needs(service):
    result = coach(service, "I check in with my officer once a month on my day off, so it won't affect my availability.",
                   "supervision")
    assert [e["key"] for e in result["elements"]] == ["acknowledge", "change", "pivot"]


def test_sealed_record_gets_disclosure_note(service):
    result = coach(service, "My record was sealed in 2020, but I made a mistake years ago and I own it. Since then I "
                            "completed my GED and I'm ready for this job.")
    assert any(i["kind"] == "legal" for i in result["issues"])


def test_proof_points_come_from_profile_without_the_setting(service):
    profile = service.call("profile.create", {"name": "Warehouse", "data": SAMPLE_PROFILE})
    proof = service.call("interview.record_proof", {"profile_id": profile["id"]})
    texts = " ".join(p["text"] + " " + p["sentence"] for p in proof)
    assert "Forklift Certification" in texts and "Pre-Apprenticeship" in texts
    assert "Correctional" not in texts  # proof names the program or job, never the facility
    assert proof[0]["sentence"].startswith("I ")


def test_save_records_practice_and_history(service):
    profile = service.call("profile.create", {"name": "P"})
    result = coach(service, STRONG, profile_id=profile["id"], save=True)
    saved = service.call("interview.saved", {"profile_id": profile["id"], "type": "record"})
    assert saved[0]["id"] == result["record"]["id"] and saved[0]["answer"] == STRONG


def test_custom_question_and_validation(service):
    result = coach(service, STRONG, "custom", custom_question="Tell me about your record.")
    assert result["question"]["question"] == "Tell me about your record."
    with pytest.raises(BridgeError) as err:
        coach(service, "", "convicted")
    assert err.value.code == "invalid"
    with pytest.raises(BridgeError) as err:
        coach(service, STRONG, "nope")
    assert err.value.code == "not_found"
