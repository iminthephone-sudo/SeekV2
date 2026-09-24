"""S.T.A.R.S coaching and assessment practice."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import BridgeError, SeekService  # noqa: E402
from seek.engines.nlp import get_nlp  # noqa: E402
from seek.storage import Store  # noqa: E402
from tests.fixtures import JOB_TEXT, SAMPLE_PROFILE  # noqa: E402

STRONG = {
    "situation": "While working as a warehouse associate at Northwest Supply in 2024, a truck arrived two hours late "
                 "during our busiest shift.",
    "task": "I had to get 40 pallets unloaded and staged before the 5pm outbound deadline.",
    "action": "I checked the manifest and organized the pallets by route. Then I operated the forklift to unload the "
              "priority freight first, and I asked a coworker to scan items with the RF scanner while I moved them.",
    "result": "We finished 20 minutes early with zero damaged items, and my supervisor thanked me in the shift meeting.",
    "skills": "This shows forklift safety, teamwork and staying calm under pressure, which is what I would bring to "
              "your team.",
}
WEAK = {"situation": "We had a big order", "action": "We kind of worked hard and stuff", "result": "It went ok"}


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


def test_question_banks(service):
    bank = service.call("interview.stars_questions")
    assert [p["key"] for p in bank["parts"]] == ["situation", "task", "action", "result", "skills"]
    ids = {q["id"] for q in bank["questions"]}
    assert {"teamwork", "mistake", "gap", "why_hire"} <= ids
    items = service.call("interview.assessment_items")
    assert len(items["scale"]) == 5 and len(items["items"]) >= 16
    assert all(i["trait"] in items["traits"] for i in items["items"])


def test_strong_answer_scores_high(service):
    job = service.call("job.from_text", {"text": JOB_TEXT, "company": "FastShip Logistics"})
    r = service.call("interview.stars_coach", {"question_id": "pressure", "answers": STRONG, "job_id": job["id"]})
    assert r["score"] >= 85 and r["grade"] == "Interview-ready"
    assert any("Connects to the posting" in g for g in r["parts"]["skills"]["good"])
    assert r["polished"].startswith("While working as a warehouse associate")
    assert 30 < r["speaking_seconds"] < 150


def test_weak_answer_gets_specific_coaching(service):
    r = service.call("interview.stars_coach", {"question_id": "pressure", "answers": WEAK})
    assert r["score"] < 40
    assert r["parts"]["task"]["score"] == 0
    assert any("“I” more than “we”" in f for f in r["parts"]["action"]["feedback"])
    assert any("number" in f for f in r["parts"]["result"]["feedback"])
    assert any("filler" in n for n in r["notes"])
    assert r["next_step"].startswith("Next, strengthen your Task")


def test_thin_answer_is_not_rated_strong(service):
    thin = {"situation": "While working at Northwest Supply in 2024, a truck showed up two hours late on our busiest day.",
            "task": "I had to get the freight unloaded before the outbound trucks left.",
            "action": "We all jumped in. I operated the forklift to move the priority pallets first and kind of kept "
                      "everyone organized.",
            "result": "We got it done on time and my supervisor was happy.", "skills": "Forklift and teamwork."}
    r = service.call("interview.stars_coach", {"question_id": "pressure", "answers": thin})
    assert r["score"] < 75 and r["grade"] == "Good start"


def test_setting_language_is_coached_but_allowed_for_gap_question(service):
    answers = dict(STRONG, situation="While I was in a correctional facility kitchen crew in 2022, we were short-staffed.")
    normal = service.call("interview.stars_coach", {"question_id": "pressure", "answers": answers})
    assert any("lead with the job" in n for n in normal["notes"])
    gap = service.call("interview.stars_coach", {"question_id": "gap", "answers": answers})
    assert any("honest about your background" in n for n in gap["notes"])


def test_custom_question_and_validation(service):
    r = service.call("interview.stars_coach", {"question_id": "custom", "custom_question": "Why do you want to work here?",
                                               "answers": STRONG})
    assert r["question"]["question"] == "Why do you want to work here?"
    with pytest.raises(BridgeError) as err:
        service.call("interview.stars_coach", {"question_id": "teamwork", "answers": {}})
    assert err.value.code == "invalid"
    with pytest.raises(BridgeError) as err:
        service.call("interview.stars_coach", {"question_id": "nope", "answers": STRONG})
    assert err.value.code == "not_found"


def test_story_ideas_come_from_profile(service):
    p = service.call("profile.create", {"data": SAMPLE_PROFILE})
    ideas = service.call("interview.story_ideas", {"profile_id": p["id"], "question_id": "leadership"})
    assert ideas and ideas[0]["text"].startswith("Trained 15 new crew members")
    assert ideas[0]["starter"]["action"].startswith("I trained 15 new crew members")


def test_saving_practice(service):
    p = service.call("profile.create", {"data": SAMPLE_PROFILE})
    saved = service.call("interview.stars_coach", {"question_id": "teamwork", "answers": STRONG,
                                                   "profile_id": p["id"], "save": True})
    rows = service.call("interview.saved", {"profile_id": p["id"], "type": "stars"})
    assert rows[0]["id"] == saved["record"]["id"] and rows[0]["score"] == saved["score"]
    assert any(h["kind"] == "interview" for h in service.call("history.list"))
    service.call("interview.delete", {"record_id": saved["record"]["id"]})
    assert service.call("interview.saved", {"profile_id": p["id"]}) == []


def test_assessment_scoring_and_flags(service):
    ideal = {"a1": 5, "a2": 1, "a3": 1, "a4": 5, "a6": 4, "a7": 2, "a18": 4, "a19": 3}
    r = service.call("interview.assessment_score", {"answers": ideal})
    assert r["consistency"] == "Consistent" and not r["flags"]
    rel = next(t for t in r["traits"] if t["key"] == "reliability")
    assert rel["score"] >= 85

    messy = {"a1": 5, "a2": 5, "a3": 4, "a18": 5, "a19": 5}
    r = service.call("interview.assessment_score", {"answers": messy, "save": True})
    kinds = {f["kind"] for f in r["flags"]}
    assert {"inconsistent", "too_good", "integrity"} <= kinds
    assert r["consistency"] == "Some contradictions"
    assert next(i for i in r["items"] if i["id"] == "a3")["fit"] == "concern"
    assert r["record"]["type"] == "assessment"

    same = {f"a{i}": 3 for i in range(1, 11)}
    assert "same_answer" in {f["kind"] for f in service.call("interview.assessment_score", {"answers": same})["flags"]}
    with pytest.raises(BridgeError):
        service.call("interview.assessment_score", {"answers": {"a1": 9}})
