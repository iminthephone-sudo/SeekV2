# SEEK bridge contract (v1)

The Qt/C++ shell and the Python engine pack talk through **one** path:
`python-backend/seek_cpp_bridge.py`. There is no alternate bridge, no HTTP
server and no Python UI. This file is the contract both sides implement:
`shell/src/core/SeekBridge.cpp` on the C++ side, and `python-backend/seek/bridge.py`
on the Python side.

## Process lifecycle (owned by the shell)

```
python python-backend/seek_cpp_bridge.py --no-qt [--data-dir DIR]
```

* The shell starts exactly one engine process at launch (`SeekBridge::start`).
* Interpreter lookup order: Settings → `SEEK_PYTHON` → `python-backend/.venv` → `python3`/`python`/`py` on PATH
  (the Windows Store `python.exe` stub is skipped).
* Backend lookup order: `SEEK_BACKEND_DIR` → Settings → `<exe dir>/python-backend` (packaged) → parent folders (dev
  builds) → the source tree path compiled into debug builds.
* Environment: `PYTHONUNBUFFERED=1`, `PYTHONIOENCODING=utf-8`, `PYTHONUTF8=1`.
* Shutdown: the shell sends `system.shutdown` and closes stdin, then kills the process after 2 s if it hasn't exited.
* If the engine crashes after it was ready, the shell restarts it automatically (at most twice) and fails every
  pending request with `engine_stopped`.
* If the engine can't start, the UI still opens and explains why (Home and Settings → Engine).

## Wire format

Everything is UTF-8, one message per line.

**Shell → engine (stdin):** a JSON object per line.

```json
{"id": "7", "method": "profile.get", "params": {"profile_id": "prof_1a2b3c4d5e6f"}}
```

**Engine → shell (stdout):** prefixed packets per line.

| Prefix | Meaning |
|---|---|
| `SEEK_JSON:` | A reply, or the start-up `ready` event |
| `SEEK_PROGRESS:` | Progress for a long-running request (`job.fetch`) |

```text
SEEK_JSON:{"id":null,"ok":true,"event":"ready","result":{...system.status...}}
SEEK_JSON:{"id":"7","ok":true,"result":{...}}
SEEK_JSON:{"id":"8","ok":false,"error":{"code":"not_found","message":"profile not found: prof_x"}}
SEEK_PROGRESS:{"id":"9","percent":40,"message":"Downloading posting…"}
```

**stderr** carries free-form diagnostics only. The shell shows them in Settings → Logs.

At start-up the engine points `sys.stdout` at stderr, so a stray `print()` in any library can't corrupt the
protocol stream. The shell ignores any stdout line without a known prefix (and logs it).

Requests are processed in order, one at a time. The shell may queue requests while the engine is still starting;
they are flushed once `ready` arrives.

## Error codes

| Code | When |
|---|---|
| `bad_json` | The request line wasn't valid JSON (`id` is `null`) |
| `bad_request` | Missing `method`, or the request isn't an object |
| `unknown_method` | No such method |
| `invalid_params` | Params don't match the method signature |
| `invalid` | Params had the right shape but a bad value (bad id, bad status, unsupported format) |
| `not_found` | Profile / job / letter doesn't exist |
| `fetch_failed` | URL import failed (bad link, unreachable, no description found); the message is written for staff |
| `fetch_blocked` | The site refused a non-browser client (HTTP 401/403/429/999). The shell opens the posting in its built-in browser and sends the page to `job.from_html`; without Qt WebEngine it shows the message and the paste box |
| `engine_error` | Engine-side failure with a readable message (e.g. python-docx missing) |
| `internal` | Unexpected exception (traceback on stderr) |
| `engine_stopped` | Produced by the **shell** when the process isn't running or died mid-request |

## Methods

Every id is `[A-Za-z0-9_-]{1,64}`. Anything else is rejected before touching the filesystem.

### System and settings
| Method | Params | Result |
|---|---|---|
| `system.ping` | – | `{pong, version}` |
| `system.status` | – | `{version, protocol, python, data_dir, nlp:{model, mode, spacy_version, skills_known, install_hint}, counts, active_profile}` |
| `system.methods` | – | list of method names |
| `system.shutdown` | – | `{bye:true}`, then the process exits |
| `settings.get` / `settings.update` | – / `{changes}` | engine-side settings document |

### Profiles (multiple profiles per participant)
| Method | Params | Result |
|---|---|---|
| `profile.schema` | – | section/field names and templates |
| `profile.list` | – | `[{id, name, participant, full_name, headline, updated_at, active, experience_count, skills_count}]` |
| `profile.get` | `profile_id` | full profile |
| `profile.create` | `name?`, `participant?`, `data?` | new profile (the first one becomes active) |
| `profile.update` | `profile_id`, `changes` | updated profile (`contact` and `options` merge; other keys replace) |
| `profile.duplicate` | `profile_id`, `name?` | the copy |
| `profile.delete` | `profile_id` | `{deleted}` |
| `profile.set_active` / `profile.active` | `profile_id` / – | active profile |
| `profile.set_account` | `profile_id`, `site` = `linkedin`\|`indeed`, `status` = `signed_in`\|`signed_out`\|`unknown` | `accounts{site:{status, checked_at}}` — the shell checks the sign-in in its browser; the engine only remembers the status (never a password) |

### Writing help (spaCy)
| Method | Params | Result |
|---|---|---|
| `assist.summary` | `profile_id`, `job_id?`, `text?`, `data?` (unsaved editor values) | `{facts, review[{kind, message}], drafts[{label, text, words}], note}` |
| `assist.duties` | `title?`, `bullets[]`, `current?`, `job_id?` | `{occupation, tense, bullets[{original, rewrite, changes[], warnings[], needs_number, review}], suggestions[{text, source}], note}` |
| `assist.rewrite_bullet` | `text`, `past?`, `placeholder?` | one rewritten bullet (as in `assist.duties`) |

### Resume
| Method | Params | Result |
|---|---|---|
| `resume.render` | `profile_id`, `format` = `html`\|`markdown`\|`text`, `template?` | `{format, content, template}` |
| `resume.export` | `profile_id`, `format` = `docx`\|`html`\|`md`\|`txt`, `path`, `template?` | `{path}` (the shell prints PDFs itself) |
| `resume.analyze` | `profile_id` | `{score, checks[], bullets[{text, score, issues[]}], suggested_skills[]}` |
| `fairchance.review` | `profile_id`, `gap_months?` | `{findings[], gaps[], guidance[], summary}` |
| `fairchance.guidance` | – | guidance cards |

### Jobs
| Method | Params | Result |
|---|---|---|
| `job.fetch` | `url` | saved job (emits `SEEK_PROGRESS`) |
| `job.from_html` | `html`, `url?` | saved job, from a page the shell loaded in its built-in browser |
| `search.run` | `query`, `location?`, `sources?` (`linkedin`, `indeed`), `page?`, `fair_chance?`, `direct?` | `{results[{source, id, title, company, location, posted, posted_date, salary, snippet, url, saved}], browser[{source, url}], errors[{source, message}]}` — sources not in `direct`, or that block the download, come back under `browser` for the shell to load (emits `SEEK_PROGRESS`) |
| `search.parse` | `source`, `html` | `{results[…]}` from a search page the shell loaded in its browser |
| `search.sources` | – | `{linkedin:{label, per_page}, indeed:{…}}` |
| `job.page_url` | `url` | the link to open for a posting (Indeed search/click links become `/viewjob?jk=`) |
| `job.from_text` | `text`, `title?`, `company?`, `location?`, `url?` | saved job |
| `job.list` / `job.get` / `job.delete` | – / `job_id` / `job_id` | |
| `job.update` | `job_id`, `changes` (`status`, `notes`, `title`, `company`, `description`…) | updated job |
| `job.statuses` | – | `saved, applying, applied, interview, offer, hired, closed` |
| `job.keywords` | `text`, `top?` | ad-hoc keyword extraction |

A job record includes `keywords[{term, key, score, kind: skill|phrase, category, required}]`,
`sections{responsibilities, requirements, benefits}` and
`signals{fair_chance, background_check, drug_screen, driving_record, license_required, exclusions[]}`.

### Optimizer and cover letters
| Method | Params | Result |
|---|---|---|
| `optimize.match` | `profile_id`, `job_id` | `{score, grade, matched[], missing[], add_to_skills[], confirm_skills[], bullet_ranking[], suggested_headline, tips[]}` |
| `optimize.apply` | `profile_id`, `job_id`, `add_skills[]`, `headline?`, `reorder?`, `as_copy?` | `{profile, match}` |
| `letter.generate` | `profile_id`, `job_id?`, `tone?`, `hiring_manager?`, `availability?`, `fair_chance_line?`, `personal_note?`, `save?` | `{text, word_count, evidence_count, match_score, letter?}` |
| `letter.save` / `letter.get` / `letter.list` / `letter.delete` | … | |
| `letter.export` | `letter_id`, `format` = `docx`\|`txt`, `path` | written path |
| `letter.tones` | – | `professional, warm, direct` |

### Interview practice (S.T.A.R.S and workplace assessment)
| Method | Params | Result |
|---|---|---|
| `interview.stars_questions` | – | `{questions[{id, category, question, looking_for, keywords, tip, sensitive?}], parts[{key, label, hint}]}` |
| `interview.story_ideas` | `profile_id`, `question_id`, `custom_question?` | profile bullets/training ranked for the question, each with a `starter{situation, action, result}` |
| `interview.stars_coach` | `question_id` (or `custom`), `answers{situation, task, action, result, skills}`, `custom_question?`, `job_id?`, `profile_id?`, `save?` | `{score, grade, parts{<part>:{score, words, good[], feedback[]}}, notes[], next_step, polished, word_count, speaking_seconds, record?}` |
| `interview.assessment_items` | – | `{traits{key:{label, about}}, items[{id, trait, text, reverse, pair?, absolute?, coaching}], scale[5]}` |
| `interview.assessment_score` | `answers{item_id: 1..5}`, `profile_id?`, `save?` | `{traits[{key, label, score}], items[{id, answer, fit: strong\|ok\|concern, coaching}], flags[{kind: inconsistent\|too_good\|same_answer\|integrity, message}], consistency, strengths[], growth[], tips[], record?}` |
| `interview.record_questions` | – | `{questions[{id, category, question, looking_for, tip, example, moves[]}], elements[{key, label, hint}], legal_note}` |
| `interview.record_coach` | `question_id` (or `custom`), `answer`, `custom_question?`, `profile_id?`, `save?` | `{score, grade, elements[{key, label, present, score, feedback, sentences[]}], issues[{kind, message, found}], outline[…], sentences[{text, moves[], tense}], proof[], next_step, legal_note, word_count, speaking_seconds, record?}` |
| `interview.record_proof` | `profile_id` | `[{kind, text, sentence}]` — certificates, training and work to cite as proof of change (never a facility name) |
| `interview.saved` / `interview.delete` | `profile_id?`, `type?` = `stars`\|`assessment`\|`record` / `record_id` | saved practice, newest first |

### History
| Method | Params | Result |
|---|---|---|
| `history.list` | `limit?`, `kind?` | newest first: `[{id, at, kind, summary, data}]` |

## Trying it by hand

```bash
cd python-backend
python seek_cpp_bridge.py --no-qt --call system.status
python seek_cpp_bridge.py --no-qt --call job.keywords --params '{"text": "Must have CDL Class A and forklift experience."}'
printf '%s\n' '{"id":"1","method":"profile.list"}' | python seek_cpp_bridge.py --no-qt
```
