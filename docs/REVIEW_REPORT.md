# SEEK: new features and adversarial review — report

Date: 2026-09-25 · Branch: `claude/beautiful-bohr-k6x0aa`

## 1. Summary

| | |
|---|---|
| New features | Record-question interview coaching · LinkedIn/Indeed sign-ins per profile · Job search (LinkedIn + Indeed) · spaCy writing help for the summary and job duties |
| Review | Two independent reviewers (engine and shell) who had not written the code, told to prove each bug with a repro |
| Findings | **48 from the review** (25 engine, 23 shell) **+ 11 found while building** = 59 |
| Fixed | **All 59**, apart from three low-severity items where the report says what was changed instead (L-1, L-2, L-3 in §5) |
| Tests | Python suite went from 39 to **113 tests, all passing**, including one regression test per engine finding (`tests/test_review_fixes.py`) |
| Builds | The shell builds with **no warnings** both with and without Qt WebEngine |
| End to end | The real app was driven through every new feature against local HTTPS stand-ins for linkedin.com and indeed.com. These include Cloudflare-style gating and an Indeed sign-in form: sign-in → check → summary help → duty help → record coaching → search (2 LinkedIn + 2 Indeed results) → import of an Indeed result. It passed before and after the fixes, with a clean exit |

**The four most serious bugs found and fixed:**

1. **The tool could claim a credential the participant doesn't have.** A job asked for "forklift certification". The profile had "forklift" in one bullet and "certification" in another. The optimizer counted it as a match, and the cover letter wrote *"Your posting calls for forklift certification, and that is an area where I already have real experience."*
2. **The record coach scored a denial of a conviction as a perfect honest answer.** *"I have never been convicted… my record is clean"* scored 100 / "Ready to say out loud".
3. **"Sign out" didn't sign the participant out.** It missed cookies saved in an earlier session, so the next staff member could search and import as that participant.
4. **Closing the built-in browser while it was capturing a page crashed SEEK** (confirmed with AddressSanitizer).

## 2. New features

### 2.1 Interview page → "Talking about your record"
- **Questions.** Seven common questions ("Have you ever been convicted…?", the background check, what's changed, "Why should we trust you…?", the gap, parole/probation scheduling, the application box), plus your own.
- **The four moves.** spaCy sorts each sentence into the moves that question calls for: *answer honestly → own it → show change → turn to the job*. It checks sentence order and verb tense: the answer should end in the present or future.
- **What it flags:**
  - blame, minimizing and "mistakes were made" wording
  - too much detail about the offense or the court case
  - jail slang, with plain-word swaps ("caught a case" → "was charged")
  - filler words
  - answers that are too long, too short, or mostly about the past
  - denials, which are capped at 40 with a clear warning
  - disputes of a background report, which get guidance on the right to dispute under the Fair Credit Reporting Act
  - sealed or expunged records, which get a disclosure note
- **Proof of change.** Taken from the profile's certificates, training, education, work and volunteering. It never uses a facility name; one click adds a sentence to the answer.
- **Know your rights.** A note on what may not need to be disclosed, plus "never lie".
- **Model answers and saving.** Every question has a model answer, and all seven score 100 (enforced by a test). Practice can be saved, reopened from "Saved practice", and is logged to history.

### 2.2 Profiles page → "Job sites" tab (LinkedIn and Indeed)
- **How sign-in works.** "Sign in" opens the site's **own** sign-in page in SEEK's built-in browser. SEEK never sees or stores the password; it stores only the status: signed in, signed out, or couldn't tell.
- **Separate storage.** Each SEEK profile has its own browser storage per site, so participants' accounts never mix. Imports and searches use the *active* profile's sign-ins.
- **Buttons.**
  - "Check" asks the site. Error and bot-check pages count as "couldn't tell", not "signed in".
  - "Sign out" wipes that site's storage for the profile.
- **Deleting a profile** wipes its browser cookies and cache immediately, and its storage folders at the next start.

### 2.3 Jobs page → "Search jobs"
- **Search.** Keywords, location, LinkedIn and/or Indeed, and an optional "only fair-chance postings".
- **How each site is read.** LinkedIn's public results feed is downloaded directly. Indeed's results page loads in a hidden built-in browser page (with the profile's Indeed sign-in) and is read from Indeed's embedded data or the result cards. If Indeed shows an "are you human" check, a **Finish Indeed's check** button opens it in a window.
- **Results.** Double-click to import. Postings already saved are marked ✓. "More results" loads the next page.
- **LinkedIn links now import.** Any LinkedIn job link (view page, search page with `currentJobId`) is downloaded from LinkedIn's public posting page. The normal view page blocks automated readers (HTTP 999). The saved link stays the normal view page.

### 2.4 Profiles page → spaCy writing help
- **Summary: "Write with spaCy".**
  - Reviews the current summary: length, "I/my", clichés, words that name the setting, missing skills or target role.
  - Offers 2–3 drafts built **only from facts in the profile**: most recent titles, time worked (counted conservatively), skills, certificates, and strengths from saved assessment practice. They can be aimed at a saved posting using that posting's wording for skills the participant really has.
  - Uses unsaved edits too.
- **Duties: "Improve duties with spaCy"** (Experience and Volunteer).
  - Rewrites each bullet: weak openers ("Responsible for…", "Duties included…") become action verbs, and "I" is dropped.
  - Uses the right tense: past for old jobs, present for the current one.
  - Puts a `[#]` prompt where a number belongs. Resume analysis flags any `[#]` left behind.
  - Suggests typical duties from a 31-occupation bank matched to the job title, plus the chosen posting's responsibilities.
  - Nothing changes until staff tick and apply.

## 3. How the review was run

- **Two separate reviewers:**
  - one for the Python engine (`python-backend/seek/**`)
  - one for the Qt shell (`shell/src/**`, `CMakeLists.txt`)
- **Their brief.** They hadn't written the code, were told to assume bugs exist, and had to prove each one with a repro. They made no edits.
- **Verification.** Every finding was re-verified before fixing. The engine repro scripts were re-run, and the two high-severity shell findings were reproduced with the reviewer's harnesses (cookie database inspection; AddressSanitizer). Findings the reviewers marked "plausible" were checked against the code before being fixed.
- **After fixing:**
  - the full test suite passed
  - both shell builds were clean
  - the sign-out and crash harnesses were re-run
  - the whole app was driven end to end again

Severity: **H** high · **M** medium · **L** low.

## 4. Engine findings (Python) — 25, all fixed

| # | Sev | Problem | Fix |
|---|---|---|---|
| E1 | H | Optimizer and cover letter matched multi-word skills and credentials from words scattered across the profile, so letters claimed certificates the participant doesn't have | Credentials and multi-word skills must appear as a phrase (`needs_exact`); used by optimizer, letters, summary drafts and interview |
| E2 | H | Record coach scored denials ("never been convicted", "left that box unchecked") as honest, 100 | Denials are detected: "Answer honestly" is not counted, score capped at 40, and a clear warning is shown. Disputing a report gets Fair Credit Reporting Act dispute guidance |
| E3 | H | Bullet rewriting corrupted correct verbs ("Broke" → "Breaked", "Ring up" → "Red up", "installed framing" → "installed framed"). This included the built-in duty suggestions | 110+ irregular verbs; a verb already in the target tense is never touched; base forms are checked by inflecting them back, otherwise the word is left alone. Every duty in the bank round-trips through both tenses (test) |
| E4 | M | Year-only dates overstated experience ("2019–2020" → "2+ years") | Year-only dates count from mid-year, so 2019–2020 = 12 months |
| E5 | M | "Responsible for building maintenance" → "Built maintenance"; "spring cleaning" → "spre" | An "-ing" word counts as a verb only if it really is one and isn't a known noun ("building", "morning", "ceiling", …) |
| E6 | M | The legally required "Fair Chance Ordinance" notice marked a job as a fair-chance employer. The letter then hinted at a record by default | Ordinance and legal wording is now a separate `fair_chance_law` signal, and only employer self-description counts |
| E7 | M | Record coach flagged innocent phrases ("knife skills", "2,000 pounds of freight", "the system", "did time management") | Tighter patterns. Detail and blame checks only apply to sentences about the past, not proof of change |
| E8 | M | A crash mid-save left `.tmp-*.json` files that later appeared as duplicate or ghost records | Temp files end in `.tmp`, dotfiles and non-records are skipped, writes are fsynced before the rename |
| E9 | M | A network error mid-download came back as "internal"; a slow server could stall the engine for 40 s+ | Download errors become readable import errors; 30-second total deadline |
| E10 | M | HTML-escaped JSON-LD descriptions kept raw tags | Unescape before parsing |
| E11 | M/L | Letters included `[#]` placeholders and broken grammar ("I also responsible for…") | Evidence bullets are rewritten; `[#]` and non-action bullets are skipped |
| E12 | L/M | Curly apostrophes (from Word or phones) stopped moves being recognised (score 100 → 80) | Quotes are normalised before analysis |
| E13 | L | A tailored copy inherited the source profile's sign-in status | Copies start signed out |
| E14 | L | Gap finder ignored education with only an end date; gaps one month too long | Fixed both |
| E15 | L | Dates like "2019-2021", "2019-05", "Summer 2020" were misread; "0000" crashed | New date parser handling ranges, ISO dates and seasons; years limited to 1900–2100 |
| E16 | L | "Cardiac arrest" and "probation period" were flagged as legal history | Excluded |
| E17 | L | "a Assistant Cook", "a 10-hour OSHA 10", the role repeated as a skill, a draft for an empty profile | Shared a/an helper (handles acronyms); role removed from the skills list; no draft for an empty profile |
| E18 | L | Any internal error was reported to the shell as "not found"; one bad record file broke whole lists | Dedicated `NotFound`; damaged records are skipped or reported plainly |
| E19 | L | `null` or list values in JSON-LD crashed the import | Values converted safely |
| E20 | L | The skill "record keeping" was dropped as policy wording | Policy filter doesn't apply to known skills |
| E21 | L | `indeed.com` vs `www.indeed.com` and `:443` links weren't treated as the same posting | Host names normalised |
| E22 | L | Control characters pasted from Word or PowerPoint broke .docx export | Removed on input |
| E23 | L | A string passed instead of a list was split into letters ("R","F",…); `None` text crashed; one bad byte of input stopped the engine | List and text parameters are normalised; invalid UTF-8 bytes are replaced instead of stopping the engine |
| E24 | L | Import would fetch localhost and private-network addresses, including via redirect | Refused, and re-checked on every redirect hop (development override: `SEEK_ALLOW_LOCAL_FETCH=1`) |
| E25 | L | Ids with a trailing newline passed validation; Windows device names (CON, NUL) were allowed | Ids must match completely; device names refused |

## 5. Shell findings (Qt/C++) — 23, all fixed

| # | Sev | Problem | Fix |
|---|---|---|---|
| S1 | H | "Sign out" missed cookies saved in an earlier session, leaving the participant signed in | Separate browser storage per profile **and site**; sign-out wipes that site's storage. Verified: after a restart, `li_at` was removed and the Indeed cookie kept. Sign out is always available |
| S2 | H | Closing the browser dialog during capture crashed SEEK and sent an empty page to import | Callbacks do nothing once the dialog or grabber is closing, or the page is being torn down. Verified with AddressSanitizer (clean exit) |
| S3 | H | A hand-written cover letter could be lost (Save on exit) or overwritten (Save, then New) | `letter.save` creates the letter in one call; stale replies are ignored; close waits for the save |
| S4 | M | "Apply to bullets" before suggestions loaded wiped all bullets | Apply is disabled until loaded and never applies an empty list |
| S5 | M | Profile editor went stale after the Optimizer changed the profile; the next save undid those changes | Reload on return when there are no unsaved edits |
| S6 | M | A slow save overwrote edits typed meanwhile, and could apply duty rewrites to the wrong entry | Save reply only repopulates if nothing changed since; sections keep their selection; duty rewrites apply by entry id |
| S7 | M | "Save" when closing the app could be cut off by the engine shutdown | The window stays open until the save is confirmed, then closes |
| S8 | M | "Discard" left the discarded edits on screen (no longer marked unsaved) | Discard reloads the saved version |
| S9 | M | Save chosen when switching profile, then a failed save, lost the edits | The next profile opens only after the save succeeds |
| S10 | M | Deleting a profile left its browsing history and site data on disk | Storage folders are removed at the next start (verified) |
| S11 | M | Browser objects were destroyed in the wrong order at exit | Explicit teardown: window, then pages, then profiles, then `QApplication` |
| S12 | L/M | Job notes typed just before switching postings were lost | Notes are saved to the posting they belong to |
| S13 | L/M | Two imports could run at once (Enter or double-click during an import) | One import at a time |
| S14 | L | A stale marker ticked the wrong search result | Cleared on every error and each new search |
| S15 | L | Search status kept an out-of-date "finish the check" message | Cleared |
| S16 | L | A timeout was shown as "wants to confirm a person" | Separate messages |
| S17 | L | Job titles and snippets from the internet could render as HTML (labels, toasts, chips, tooltips, prompts) | Always plain text or escaped |
| S18 | L | After Cancel, the profile tree highlighted the wrong profile | Tree rebuild deferred |
| S19 | L | Clicking Indeed's logo on its sign-in page counted as signed in | Sign-in is confirmed with the site before it's recorded |
| S20 | L | After deleting the active profile, the shell kept using its id | Follows the engine's new active profile and announces it |
| S21 | L | PDF export could print the previous profile's resume, and reported success even when nothing was written | Renders fresh for export; a write failure is reported |
| S22 | L | Interview page: an engine restart wiped answers in progress; delete could remove a different record | Question banks kept; record id captured before the prompt |
| S23 | L | CMake comment claimed `cmake --install` gives a runnable folder on Windows | Comment corrected: `scripts/build_windows.ps1` runs windeployqt for that |

## 6. Found while building the features — 11, all fixed

| Problem | Fix |
|---|---|
| Duty rewrite turned "dining room" into "dinned room" | "-ing" words describing a noun are left alone |
| Duty rewrite turned "GED" into "g" (found by the new round-trip test) | Acronyms are never verbs; empty base forms rejected |
| Duty rewrite: "Worked on the assembly line" → "Completed the assembly line" (changed meaning) | That opener is no longer rewritten |
| Sign-in "Check" called a LinkedIn error page "signed in" | HTTP status read; error pages count as "couldn't tell" |
| Duplicating a profile copied its sign-in status to a copy with empty storage | Copies start signed out |
| The job dropdown's "none" entry said "General letter" on non-letter pages | Label per page |
| Interview page used fixed tab numbers, which a new tab would have broken | Tabs referenced directly |
| After deleting a profile, its details stayed visible in the disabled editor | Editor cleared |
| The paste box's word count counted empty strings | Fixed |
| Record coach: a bare "Yes." counted as a whole sentence about the past | Balance weighed by words |
| Record coach: model answers scored low because not every question needs all four moves | Each question lists the moves it needs |

**Items handled by a clarification rather than a behaviour change:**

- **L-1:** The fair-chance line in letters stays on "auto". After E6 it only turns on for employers that call themselves fair-chance.
- **L-2:** Internal *TypeError*s still map to "invalid", so shell contract mistakes read as bad input.
- **L-3:** The Windows install folder is still produced by the build script (S23).

## 7. What could not be verified here

- **Live LinkedIn and Indeed.** This environment has no internet access. The search and import parsers, sign-in pages and "check" URLs follow the sites' known structure and passed against local stand-ins, but **have not been tested against the live sites**. The Indeed sign-in check (`profile.indeed.com`) and LinkedIn's (`/feed/`) should be confirmed on the first real run. If either site changes its markup, the fix is a selector update in `seek/engines/search.py` or `jobs.py`.
- **Windows or Qt 6.7 at runtime.** Everything ran on Linux with Qt 6.4. The reviewer found no API that differs in 6.7 or on MSVC.
- **Basic NLP mode.** This is the fallback when the spaCy model isn't installed. Every run used the full model.
- **A real power-loss test** for the fsync change.

## 8. What you need to do

1. Pull `claude/beautiful-bohr-k6x0aa`.
2. Reinstall the engine's packages: `scripts\setup_backend.ps1`. `curl_cffi` is new.
3. In the Qt Maintenance Tool, add **Qt WebEngine** to your `msvc2019_64` kit.
4. Rebuild: `.\scripts\build_windows.ps1`. It finds the newest Qt MSVC kit in `C:\Qt` or `D:\Qt` by itself, or takes `-QtDir D:\Qt\6.10.2`. It prints the kit it uses and whether Qt WebEngine is installed.
5. First live checks:
   - sign a test profile in to LinkedIn and Indeed and press **Check**
   - run a search on both sites
   - import one result from each
