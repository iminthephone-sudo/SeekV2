"""Engine tests. Run from python-backend/:  python -m pytest -q"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import BridgeError, SeekService  # noqa: E402
from seek.engines import fair_chance, jobs, resume  # noqa: E402
from seek.engines.nlp import get_nlp  # noqa: E402
from seek.storage import Store  # noqa: E402
from tests.fixtures import JOB_HTML_JSONLD, JOB_HTML_PLAIN, JOB_TEXT, SAMPLE_PROFILE  # noqa: E402


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


@pytest.fixture()
def profile(service):
    return service.call("profile.create", {"name": SAMPLE_PROFILE["name"],
                                           "participant": SAMPLE_PROFILE["participant"], "data": SAMPLE_PROFILE})


def test_profiles_crud_and_multiple(service, profile):
    assert profile["contact"]["full_name"] == "Marcus Reed"
    assert len(profile["experience"]) == 2
    second = service.call("profile.duplicate", {"profile_id": profile["id"], "name": "Kitchen focus"})
    rows = service.call("profile.list")
    assert {r["name"] for r in rows} == {"Warehouse focus", "Kitchen focus"}
    assert [r for r in rows if r["active"]][0]["id"] == profile["id"]  # first profile becomes active
    service.call("profile.set_active", {"profile_id": second["id"]})
    assert service.call("profile.active")["id"] == second["id"]
    updated = service.call("profile.update", {"profile_id": second["id"],
                                              "changes": {"contact": {"phone": "555-0000"}, "skills": "Cooking\nBaking\nCooking"}})
    assert updated["contact"]["full_name"] == "Marcus Reed"  # partial contact merge keeps other fields
    assert updated["skills"] == ["Cooking", "Baking"]         # newline text accepted, duplicates removed
    service.call("profile.delete", {"profile_id": second["id"]})
    assert service.call("profile.active")["id"] == profile["id"]


def test_create_uses_name_from_data_when_not_given(service):
    created = service.call("profile.create", {"data": SAMPLE_PROFILE})
    assert created["name"] == "Warehouse focus" and created["participant"] == "Marcus R."
    explicit = service.call("profile.create", {"name": "Override", "data": SAMPLE_PROFILE})
    assert explicit["name"] == "Override"


def test_invalid_ids_are_rejected(service):
    with pytest.raises(BridgeError) as err:
        service.call("profile.get", {"profile_id": "../../etc/passwd"})
    assert err.value.code == "invalid"
    with pytest.raises(BridgeError) as err:
        service.call("profile.get", {"profile_id": "prof_missing"})
    assert err.value.code == "not_found"
    with pytest.raises(BridgeError) as err:
        service.call("profile.get", {"wrong": 1})
    assert err.value.code == "invalid_params"


def test_resume_render_and_export(service, profile, tmp_path):
    html = service.call("resume.render", {"profile_id": profile["id"], "format": "html", "template": "modern"})["content"]
    assert "Marcus Reed" in html and "EXPERIENCE" in html and "&amp;" in html  # escaped '&'
    text = service.call("resume.render", {"profile_id": profile["id"], "format": "text"})["content"]
    assert text.startswith("MARCUS REED")
    for fmt in ("docx", "html", "md", "txt"):
        out = service.call("resume.export", {"profile_id": profile["id"], "format": fmt, "path": str(tmp_path / f"r.{fmt}")})
        assert Path(out["path"]).stat().st_size > 200


def test_resume_analysis_coaches_weak_bullets(service, profile):
    report = service.call("resume.analyze", {"profile_id": profile["id"]})
    weak = [b for b in report["bullets"] if b["text"].startswith("Responsible for")][0]
    assert weak["score"] < 60 and weak["issues"]
    strong = [b for b in report["bullets"] if b["text"].startswith("Operated forklift")][0]
    assert strong["score"] == 100
    # Skills demonstrated in bullets but not listed are suggested.
    assert "pallet jack" in report["suggested_skills"]
    assert 0 < report["score"] <= 100


def test_fair_chance_review_flags_setting_not_skill(service, profile):
    review = service.call("fairchance.review", {"profile_id": profile["id"]})
    terms = {f["term"].lower() for f in review["findings"]}
    assert "correctional facility" in terms and "inmates" in terms
    kitchen = [f for f in review["findings"] if f["field"] == "experience[0].employer"][0]
    assert kitchen.get("title_idea")  # suggests a skill-first job title
    assert review["gaps"] == []  # Dec 2023 -> Mar 2024 is under the 9-month threshold


def test_gap_detection():
    p = {"experience": [{"title": "A", "start": "2015", "end": "2016"}, {"title": "B", "start": "2019", "end": "2020"}]}
    gaps = fair_chance.find_gaps(p)
    assert len(gaps) == 1 and gaps[0]["months"] >= 24
    p["training"] = [{"name": "Welding", "date": "2018"}]
    assert all(g["months"] < 24 for g in fair_chance.find_gaps(p))


def test_job_parsing_jsonld_and_plain():
    raw = jobs.parse_html(JOB_HTML_JSONLD)
    assert raw["source"] == "json-ld" and raw["company"] == "FastShip Logistics"
    assert raw["location"] == "Tacoma, WA" and "21" in raw["salary"]
    assert "forklift certification" in raw["description"].lower()
    plain = jobs.parse_html(JOB_HTML_PLAIN)
    assert plain["title"] == "Line Cook" and plain["company"] == "Harbor Grill"
    assert "Food handler's card required" in plain["description"]
    assert "Menu" not in plain["description"]  # nav stripped


def test_job_signals_and_sections(service):
    job = service.call("job.from_text", {"text": JOB_TEXT, "company": "FastShip Logistics"})
    assert job["title"].startswith("Warehouse Associate")
    assert job["signals"]["fair_chance"] is True
    assert any("forklift certification" in r.lower() for r in job["sections"]["requirements"])
    terms = {k["term"].lower() for k in job["keywords"]}
    assert {"forklift certification", "rf scanner", "cycle counting"} <= terms
    moved = service.call("job.update", {"job_id": job["id"], "changes": {"status": "applied"}})
    assert moved["status"] == "applied"
    with pytest.raises(BridgeError):
        service.call("job.update", {"job_id": job["id"], "changes": {"status": "bogus"}})


def test_fetch_rejects_non_http(service):
    with pytest.raises(BridgeError) as err:
        service.call("job.fetch", {"url": "file:///etc/passwd"})
    assert err.value.code == "fetch_failed"


def test_optimizer_match_and_tailor(service, profile):
    job = service.call("job.from_text", {"text": JOB_TEXT, "company": "FastShip Logistics"})
    result = service.call("optimize.match", {"profile_id": profile["id"], "job_id": job["id"]})
    assert 30 <= result["score"] <= 100
    missing = {m["term"] for m in result["missing"]}
    assert "cycle counting" in missing and "OSHA 10" in missing
    assert "pallet jack" in result["add_to_skills"]  # shown in bullets, not yet listed
    applied = service.call("optimize.apply", {"profile_id": profile["id"], "job_id": job["id"],
                                              "add_skills": result["add_to_skills"] + ["cycle counting"],
                                              "headline": result["suggested_headline"]})
    assert applied["match"]["score"] > result["score"]
    tailored = applied["profile"]
    assert tailored["id"] != profile["id"] and "FastShip" in tailored["name"]
    # Bullets are reordered by relevance: the forklift/freight bullets now lead, the generic one trails.
    warehouse = tailored["experience"][1]["bullets"]
    assert warehouse[0].startswith(("Operated forklift", "Loaded and unloaded"))
    assert sorted(warehouse) == sorted(SAMPLE_PROFILE["experience"][1]["bullets"])
    # The original profile is untouched.
    assert "cycle counting" not in service.call("profile.get", {"profile_id": profile["id"]})["skills"]


def test_cover_letter(service, profile, tmp_path):
    job = service.call("job.from_text", {"text": JOB_TEXT, "company": "FastShip Logistics"})
    out = service.call("letter.generate", {"profile_id": profile["id"], "job_id": job["id"], "tone": "warm",
                                           "hiring_manager": "Ms. Lopez"})
    text = out["text"]
    assert "Dear Ms. Lopez," in text and "FastShip Logistics" in text and "Marcus Reed" in text
    assert "operated forklift" in text  # evidence drawn from the participant's own bullets
    assert out["fair_chance_line"] is True  # posting says fair chance
    assert "inmate" not in text.lower() and "correctional" not in text.lower()
    letter_id = out["letter"]["id"]
    service.call("letter.save", {"letter_id": letter_id, "text": text + "\nP.S. Edited."})
    assert service.call("letter.get", {"letter_id": letter_id})["text"].endswith("Edited.")
    path = service.call("letter.export", {"letter_id": letter_id, "format": "docx", "path": str(tmp_path / "l.docx")})
    assert Path(path).exists()
    general = service.call("letter.generate", {"profile_id": profile["id"], "save": False})
    assert "Dear Hiring Manager," in general["text"]


def test_history_is_durable(service, profile, tmp_path):
    service.call("job.from_text", {"text": JOB_TEXT})
    kinds = [h["kind"] for h in service.call("history.list")]
    assert "profile" in kinds and "job" in kinds
    # A fresh service on the same folder sees the same history.
    again = SeekService(Store(service.store.root), service.nlp)
    assert len(again.call("history.list")) == len(kinds)


def test_resume_html_is_escaped():
    p = {"contact": {"full_name": "<script>x</script>"}, "skills": [], "options": {}}
    assert "<script>" not in resume.render_html(p)
