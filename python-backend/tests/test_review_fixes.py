"""Regression tests for the adversarial review (docs/REVIEW_REPORT.md). Each test names its finding."""

from __future__ import annotations

import sys
from datetime import date
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import BridgeError, SeekService  # noqa: E402
from seek.engines import jobs  # noqa: E402
from seek.engines.fair_chance import _scan, find_gaps, parse_when  # noqa: E402
from seek.engines.nlp import article, get_nlp  # noqa: E402
from seek.storage import Store  # noqa: E402


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


def test_py1_no_credential_claimed_from_scattered_words(service):
    profile = service.call("profile.create", {"name": "P", "data": {
        "contact": {"full_name": "Pat"},
        "experience": [{"title": "Warehouse Associate", "employer": "Acme",
                        "bullets": ["Unloaded trucks alongside the forklift crew"]}],
        "certifications": [{"name": "ServSafe Food Handler Certification"}]}})
    job = service.call("job.from_text", {"title": "Forklift Operator", "company": "Beta",
                                         "text": "Forklift certification required. " * 10 + "Load trucks safely. " * 10})
    match = service.call("optimize.match", {"profile_id": profile["id"], "job_id": job["id"]})
    assert "forklift certification" not in [m["term"].lower() for m in match["matched"]]
    letter = service.call("letter.generate", {"profile_id": profile["id"], "job_id": job["id"], "save": False})
    assert "forklift certification, and that is an area" not in letter["text"]


@pytest.mark.parametrize("qid,answer", [
    ("convicted", "That's a fair question. I have never been convicted of a felony and my record is clean. Since then "
                  "I completed a 12-week program. I'm ready to bring that to your team."),
    ("application_box", "I left that box unchecked because I didn't think it applied. I made a mistake and I own it. "
                        "I'm ready to bring that to your team."),
])
def test_py2_denial_is_never_an_honest_answer(service, qid, answer):
    result = service.call("interview.record_coach", {"question_id": qid, "answer": answer})
    assert result["score"] <= 40
    assert any(i["kind"] == "denial" for i in result["issues"])
    assert not any(e["key"] == "acknowledge" and e["present"] for e in result["elements"])


def test_py2_disputed_report_gets_rights_guidance(service):
    result = service.call("interview.record_coach", {
        "question_id": "background_check",
        "answer": "That conviction is not on my record anymore, it should not be there. I take responsibility for my "
                  "past. Since then I completed a program. I'm ready to bring that to your team."})
    assert any(i["kind"] == "dispute" and "Fair Credit Reporting Act" in i["message"] for i in result["issues"])


@pytest.mark.parametrize("before,past,after", [
    ("Broke down pallets", True, "Broke down [#] pallets"),
    ("Broke down pallets", False, "Break down [#] pallets"),
    ("Told customers about specials", True, "Told [#] customers about specials"),
    ("Froze meat", True, "Froze meat"),
    ("Ring up customers at the register", True, "Rang up [#] customers at the register"),
    ("Measured, cut and installed framing and trim", False, "Measure, cut and install framing and trim"),
    ("Read blueprints and weld symbols", True, "Read [#] blueprints and weld symbols"),  # tense can't be told
    ("Responsible for building maintenance", True, "Managed building maintenance"),
    ("Helped with spring cleaning", True, "Assisted with spring cleaning"),
])
def test_py3_py5_rewrites_never_corrupt_verbs(service, before, past, after):
    out = service.call("assist.rewrite_bullet", {"text": before, "past": past})
    assert out["rewrite"] == after


def test_py3_duty_bank_survives_present_tense(service):
    for occ in service.writing.occupations:
        for duty in occ["duties"]:
            present = service.call("assist.rewrite_bullet", {"text": duty, "past": False, "placeholder": False})
            back = service.call("assist.rewrite_bullet", {"text": present["rewrite"], "past": True, "placeholder": False})
            assert back["rewrite"].split()[0] == duty.split()[0], (duty, present["rewrite"], back["rewrite"])


def test_py4_year_only_dates_do_not_inflate_experience(service):
    months = service.writing._months_worked({"experience": [{"start": "2019", "end": "2020"}]})
    assert months == 12


def test_py6_fair_chance_ordinance_is_not_a_fair_chance_employer():
    law = ("Pursuant to the San Francisco Fair Chance Ordinance, we will consider for employment qualified applicants "
           "with arrest and conviction records.")
    signals = jobs.detect_signals(law)
    assert signals["fair_chance"] is False and signals["fair_chance_law"] is True
    assert jobs.detect_signals("We are a proud fair chance employer.")["fair_chance"] is True


@pytest.mark.parametrize("phrase", ["learned knife skills in a culinary program", "moved 2,000 pounds of freight",
                                    "they put me in charge of training new cooks", "I learned the system quickly",
                                    "Only a few months after starting I was promoted", "did time management training"])
def test_py7_innocent_phrases_are_not_flagged(service, phrase):
    answer = f"Yes, I was convicted in 2018 and I take responsibility. Since then I {phrase}. I'm ready for this job."
    result = service.call("interview.record_coach", {"question_id": "convicted", "answer": answer})
    assert not {i["kind"] for i in result["issues"]} & {"overshare", "blame", "minimize", "slang"}


def test_py8_crash_leftovers_are_not_records(tmp_path):
    store = Store(tmp_path)
    store.put("profiles", "prof_a", {"id": "prof_a", "name": "Real"})
    (tmp_path / "profiles" / ".tmp-abc.json").write_text('{"id": "prof_a", "name": "Ghost"}')
    (tmp_path / "profiles" / "prof_list.json").write_text("[]")
    assert [p["name"] for p in store.all("profiles")] == ["Real"]
    assert not list((tmp_path / "profiles").glob(".tmp-*.tmp"))


def test_py10_escaped_json_ld_description_is_cleaned():
    page = ('<script type="application/ld+json">{"@type": "JobPosting", "title": "Cook", '
            '"description": "&lt;p&gt;We need a &lt;strong&gt;Line Cook&lt;/strong&gt;.&lt;/p&gt;"}</script>')
    assert "<" not in jobs.parse_html(page)["description"]


def test_py12_curly_quotes_score_the_same(service):
    straight = ("Yes, I was convicted in 2018. I take responsibility. Since then I completed a program. "
                "I'd bring that reliability to your team.")
    a = service.call("interview.record_coach", {"question_id": "convicted", "answer": straight})
    b = service.call("interview.record_coach", {"question_id": "convicted", "answer": straight.replace("'", "’")})
    assert a["score"] == b["score"]


def test_py14_py15_dates_and_gaps():
    assert parse_when("2019-2021") == date(2019, 1, 1) and parse_when("2019-2021", is_end=True) == date(2021, 12, 1)
    assert parse_when("2019-05") == date(2019, 5, 1)
    assert parse_when("Summer 2020") == date(2020, 6, 1)
    assert parse_when("0000") is None and parse_when("Present (part-time)") == date.today()
    back_to_back = {"experience": [{"title": "A", "start": "Jan 2018", "end": "Jan 2019"},
                                   {"title": "B", "start": "Feb 2019", "end": "Present"}]}
    assert find_gaps(back_to_back, gap_months=0) == []
    ged = {"experience": [{"title": "A", "start": "2015", "end": "2016"}, {"title": "B", "start": "2022", "end": "2023"}],
           "education": [{"credential": "GED", "start": "", "end": "2019"}]}
    assert all(g["months"] < 61 for g in find_gaps(ged))


def test_py16_innocent_resume_words_are_not_flagged():
    assert _scan("Trained to respond to cardiac arrest") == []
    assert _scan("Completed a 90-day probation period") == []
    assert _scan("On probation until 2027")


def test_py17_articles():
    assert [article(w) for w in ("Assistant Cook", "Usher", "RF scanner", "CDL", "OSHA card", "hour", "uniform")] == \
        ["an", "an", "an", "a", "an", "an", "a"]


def test_py18_internal_errors_are_not_reported_as_not_found(service, tmp_path, monkeypatch):
    monkeypatch.setitem(service.methods, "profile.list", lambda: {}["boom"])
    with pytest.raises(KeyError):  # surfaces as "internal" through handle_line, not "not_found"
        service.call("profile.list")
    with pytest.raises(BridgeError) as err:
        service.call("profile.get", {"profile_id": "prof_missing"})
    assert err.value.code == "not_found"


def test_py23_lone_strings_are_one_item(service):
    out = service.call("assist.duties", {"title": "Cook", "bullets": "Cooked food"})
    assert [b["original"] for b in out["bullets"]] == ["Cooked food"]
    assert service.call("job.page_url", {"url": None}) == ""


def test_py25_ids_must_match_fully():
    from seek.storage import safe_id
    assert not safe_id("prof_x\n") and not safe_id("CON") and not safe_id("nul") and safe_id("prof_abc123")
