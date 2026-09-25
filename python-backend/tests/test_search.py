"""Job search (LinkedIn feed + Indeed page parsing), LinkedIn links, and job-site sign-in status."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.bridge import BridgeError, SeekService  # noqa: E402
from seek.engines import jobs, search  # noqa: E402
from seek.engines.nlp import get_nlp  # noqa: E402
from seek.storage import Store  # noqa: E402

# Shape of LinkedIn's public search feed (jobs-guest …/seeMoreJobPostings/search).
LINKEDIN_FEED = """
<li><div class="base-card relative w-full base-card--link base-search-card job-search-card"
     data-entity-urn="urn:li:jobPosting:3712345678">
  <a class="base-card__full-link" href="https://www.linkedin.com/jobs/view/warehouse-associate-at-acme-3712345678?position=1&amp;pageNum=0">
    <span class="sr-only">Warehouse Associate</span></a>
  <div class="base-search-card__info">
    <h3 class="base-search-card__title">Warehouse Associate</h3>
    <h4 class="base-search-card__subtitle"><a class="hidden-nested-link" href="#">Acme Logistics</a></h4>
    <div class="base-search-card__metadata">
      <span class="job-search-card__location">Denver, CO</span>
      <span class="job-search-card__salary-info">$19.00 - $21.00</span>
      <time class="job-search-card__listdate" datetime="2026-09-20">5 days ago</time>
    </div></div></div></li>
<li><div class="base-card base-search-card job-search-card" data-entity-urn="urn:li:jobPosting:3799999999">
  <a class="base-card__full-link" href="https://www.linkedin.com/jobs/view/line-cook-at-diner-3799999999"></a>
  <h3 class="base-search-card__title">Line Cook</h3><h4 class="base-search-card__subtitle">The Diner</h4>
  <span class="job-search-card__location">Aurora, CO</span>
  <time class="job-search-card__listdate--new" datetime="2026-09-25">2 hours ago</time></div></li>
"""

INDEED_JSON_PAGE = ('<html><body><script>window.mosaic.providerData["mosaic-provider-jobcards"]=' + json.dumps({
    "metaData": {"mosaicProviderJobCardsModel": {"results": [
        {"jobkey": "0123456789abcdef", "displayTitle": "Forklift Operator", "company": "FastShip",
         "formattedLocation": "Denver, CO 80216", "formattedRelativeTime": "3 days ago",
         "snippet": "<ul><li>Operate sit-down forklift</li></ul>", "salarySnippet": {"text": "$20 an hour"}},
        {"jobkey": "fedcba9876543210", "title": "Picker &amp; Packer", "company": "Acme",
         "formattedLocation": "Remote"},
        {"jobkey": "not-a-key", "title": "Ignored"},
    ]}}}) + ';\nwindow.mosaic.providerData["other"]={};</script></body></html>')

INDEED_CARDS_PAGE = """<html><body><ul>
<li><div class="cardOutline"><div class="job_seen_beacon">
  <h2 class="jobTitle"><a data-jk="0123456789abcdef" class="jcs-JobTitle" href="/rc/clk?jk=0123456789abcdef">
    <span title="Forklift Operator" id="jobTitle-0123456789abcdef">Forklift Operator</span></a></h2>
  <span data-testid="company-name">FastShip</span><div data-testid="text-location">Denver, CO</div>
  <div data-testid="attribute_snippet_testid">$20 an hour</div>
  <span data-testid="myJobsStateDate">Posted 3 days ago</span>
</div></div></li></ul></body></html>"""


@pytest.fixture()
def service(tmp_path):
    return SeekService(Store(tmp_path), get_nlp())


def test_linkedin_feed_parses_cards():
    rows = search.parse_linkedin(LINKEDIN_FEED)
    assert [r["title"] for r in rows] == ["Warehouse Associate", "Line Cook"]
    first = rows[0]
    assert first["company"] == "Acme Logistics" and first["location"] == "Denver, CO"
    assert first["url"] == "https://www.linkedin.com/jobs/view/3712345678/"
    assert first["salary"] == "$19.00 - $21.00" and first["posted_date"] == "2026-09-20"


def test_indeed_embedded_json_is_preferred():
    rows = search.parse_indeed(INDEED_JSON_PAGE)
    assert [r["title"] for r in rows] == ["Forklift Operator", "Picker & Packer"]
    assert rows[0]["url"] == "https://www.indeed.com/viewjob?jk=0123456789abcdef"
    assert rows[0]["salary"] == "$20 an hour" and "sit-down forklift" in rows[0]["snippet"]


def test_indeed_rendered_cards_fallback():
    rows = search.parse_indeed(INDEED_CARDS_PAGE)
    assert rows == [{"source": "indeed", "id": "indeed:0123456789abcdef", "title": "Forklift Operator",
                     "company": "FastShip", "location": "Denver, CO", "posted": "3 days ago", "posted_date": "",
                     "salary": "$20 an hour", "snippet": "", "url": "https://www.indeed.com/viewjob?jk=0123456789abcdef"}]


def test_search_urls():
    assert search.search_url("linkedin", "line cook", "Denver, CO", page=1) == (
        "https://www.linkedin.com/jobs-guest/jobs/api/seeMoreJobPostings/search?keywords=line+cook&location=Denver%2C+CO&start=25")
    assert search.search_url("indeed", "cook", "", fair_chance=True) == (
        "https://www.indeed.com/jobs?q=cook+%22fair+chance%22&l=")


def test_search_run_downloads_direct_sources_and_hands_the_rest_to_the_browser(service, monkeypatch):
    calls = []

    def fake_fetch(url, progress=None):
        calls.append(url)
        if "linkedin" in url:
            return LINKEDIN_FEED, url
        raise jobs.FetchError("blocked", blocked=True)

    monkeypatch.setattr(search, "fetch_url", fake_fetch)
    out = service.call("search.run", {"query": "warehouse", "location": "Denver", "direct": ["linkedin", "indeed"]})
    assert len(out["results"]) == 2 and out["errors"] == []
    assert [b["source"] for b in out["browser"]] == ["indeed"]  # blocked -> built-in browser
    calls.clear()
    out = service.call("search.run", {"query": "warehouse", "direct": ["linkedin"]})
    assert all("indeed" not in c for c in calls)  # not even tried when the shell will load it
    assert out["browser"][0]["url"].startswith("https://www.indeed.com/jobs?q=warehouse")


def test_search_marks_already_saved_postings(service, monkeypatch):
    monkeypatch.setattr(search, "fetch_url", lambda url, progress=None: (LINKEDIN_FEED, url))
    service.call("job.from_text", {"text": "word " * 40, "title": "Warehouse Associate",
                                   "url": "https://www.linkedin.com/jobs/view/warehouse-associate-at-acme-3712345678"})
    out = service.call("search.run", {"query": "warehouse", "sources": ["linkedin"]})
    assert [r["saved"] for r in out["results"]] == [True, False]


def test_search_parse_and_validation(service):
    out = service.call("search.parse", {"source": "indeed", "html": INDEED_CARDS_PAGE})
    assert out["results"][0]["id"] == "indeed:0123456789abcdef"
    with pytest.raises(BridgeError) as err:
        service.call("search.run", {"query": "   "})
    assert err.value.code == "invalid"
    with pytest.raises(BridgeError) as err:
        service.call("search.parse", {"source": "monster", "html": ""})
    assert err.value.code == "invalid"


@pytest.mark.parametrize("link", [
    "https://www.linkedin.com/jobs/view/warehouse-associate-at-acme-3712345678?position=1&refId=x",
    "https://www.linkedin.com/jobs/search/?currentJobId=3712345678&keywords=cook",
    "https://www.linkedin.com/jobs-guest/jobs/api/jobPosting/3712345678",
])
def test_linkedin_links_normalize_and_download_from_guest_page(link):
    assert jobs.normalize_url(link) == "https://www.linkedin.com/jobs/view/3712345678/"
    assert jobs.download_url(jobs.normalize_url(link)) == \
        "https://www.linkedin.com/jobs-guest/jobs/api/jobPosting/3712345678"


LINKEDIN_POSTING = """<section class="top-card-layout">
<h2 class="top-card-layout__title">Warehouse Associate</h2>
<a class="topcard__org-name-link" href="#">Acme Logistics</a>
<span class="topcard__flavor topcard__flavor--bullet">Denver, CO</span></section>
<div class="show-more-less-html__markup"><p>Join our team.</p><ul><li>Pick and pack customer orders with an RF scanner</li>
<li>Load and unload trucks safely with pallet jacks</li><li>Keep aisles clean and organized every shift</li></ul>
<p>Requirements: forklift certification preferred, able to lift 50 pounds.</p></div>"""


def test_linkedin_fetch_uses_guest_page_but_keeps_view_link(service, monkeypatch):
    seen = []

    def fake_fetch(url, progress=None):
        seen.append(url)
        return LINKEDIN_POSTING, url

    monkeypatch.setattr(jobs, "fetch_url", fake_fetch)
    job = service.call("job.fetch", {"url": "https://www.linkedin.com/jobs/view/warehouse-associate-at-acme-3712345678"})
    assert seen == ["https://www.linkedin.com/jobs-guest/jobs/api/jobPosting/3712345678"]
    assert job["url"] == "https://www.linkedin.com/jobs/view/3712345678/"
    assert (job["title"], job["company"], job["location"]) == ("Warehouse Associate", "Acme Logistics", "Denver, CO")
    assert "Pick and pack" in job["description"]


def test_account_status_is_stored_survives_saves_and_logs_history(service):
    profile = service.call("profile.create", {"name": "P"})
    accounts = service.call("profile.set_account", {"profile_id": profile["id"], "site": "linkedin", "status": "signed_in"})
    assert accounts["linkedin"]["status"] == "signed_in" and accounts["linkedin"]["checked_at"]
    # A normal profile save from the editor (which doesn't send accounts) keeps them.
    updated = service.call("profile.update", {"profile_id": profile["id"], "changes": {"headline": "Cook"}})
    assert updated["accounts"]["linkedin"]["status"] == "signed_in"
    assert any("Signed in to LinkedIn" in h["summary"] for h in service.call("history.list"))
    with pytest.raises(BridgeError) as err:
        service.call("profile.set_account", {"profile_id": profile["id"], "site": "myspace", "status": "signed_in"})
    assert err.value.code == "invalid"
