# SEEK architecture

SEEK follows the platform rules in `Agent-memory-Canon.md`:

* a **Qt/C++ shell** that owns every window
* **Python engines** behind one explicit bridge contract
* strict boundaries between the layers

```
┌──────────────────────────── shell/ (C++ · Qt 6 Widgets) ─────────────────────────────┐
│ MainWindow: frameless Fluent window, TitleBar, Sidebar, InfoBar toasts               │
│ Pages: Home · Profiles · Resume · Jobs · Optimizer · Letters · Activity · Settings   │
│ AppContext: live view-model cache (profiles, jobs, active profile, navigation)       │
│ Theme: FancyUI palette tokens → QSS + tinted SVG icons (light/dark/system)           │
│ SeekBridge: QProcess lifecycle, request ids, callbacks, progress, diagnostics        │
└───────────────┬──────────────────────────────────────────────────────────────────────┘
                │  stdin: JSON request per line
                │  stdout: SEEK_JSON: / SEEK_PROGRESS: packets    stderr: diagnostics
┌───────────────▼────────── python-backend/ (engine pack, --no-qt) ────────────────────┐
│ seek_cpp_bridge.py → seek/bridge.py (method registry, error mapping, packet I/O)     │
│ engines/nlp.py         spaCy pipeline, skill PhraseMatcher, keyword ranking, coaching │
│ engines/profiles.py    multiple profiles per participant                              │
│ engines/resume.py      HTML/Markdown/text/DOCX rendering + analysis                   │
│ engines/jobs.py        URL import (JSON-LD → HTML heuristics) / paste, signals        │
│ engines/optimizer.py   match score, gaps, honest tailoring                            │
│ engines/cover_letter.py letters built from the participant's own bullets              │
│ engines/fair_chance.py skill-first wording review, gap detection, staff guidance      │
│ engines/interview.py   S.T.A.R.S answer coaching + workplace-assessment practice      │
│ storage.py             local JSON documents (atomic writes) + history.jsonl           │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## Ownership

| Layer | Owns | Never does |
|---|---|---|
| Shell | Windows, navigation, theme, the engine process lifecycle, printing PDFs, file dialogs, progress UI | Parse postings, score, write letters, touch the data files |
| Bridge | The wire contract (`docs/BRIDGE_CONTRACT.md`), validation, error codes | Business logic |
| Engines | Profiles, resume rendering, NLP, job import, optimization, letters, history | Open windows or import Qt |

## Data and privacy

Participant data is sensitive, so everything stays on the machine:

| OS | Default data folder |
|---|---|
| Windows | `%APPDATA%\SEEK` |
| macOS | `~/Library/Application Support/SEEK` |
| Linux | `$XDG_DATA_HOME/SEEK` or `~/.local/share/SEEK` |

You can override it with Settings → Engine → Data folder, `--data-dir`, or `SEEK_DATA_DIR`.

```
SEEK/
  profiles/prof_*.json   one document per profile
  jobs/job_*.json        saved postings with keywords and signals
  interview/int_*.json   saved S.T.A.R.S answers and assessment practice
  letters/ltr_*.json     cover letters
  settings/app.json      engine-side settings (active profile)
  history.jsonl          append-only activity log (durable history for bridge-fed workflows)
```

Nothing is uploaded. The only network access is `job.fetch`, which downloads the posting a staff member asked for.
When a site blocks that download (Indeed), the shell opens the same posting in its built-in browser
(Qt WebEngine, `ui/BrowserImportDialog`) and hands the rendered page to `job.from_html`.

## AI / NLP

SEEK's language features run on spaCy (`en_core_web_sm` by default; set `SEEK_SPACY_MODEL` to use a larger model).

* **Skill detection:** a `PhraseMatcher` over `seek/data/skills.json`, with 200+ skills weighted toward trades,
  logistics, food service, retail, office and human-services roles, plus aliases (`ms office` → Microsoft Office,
  `loto` → lockout/tagout).
* **Keyword ranking:** lexicon skills plus noun chunks and proper nouns. Postings are parsed line by line, so a
  "Must have…" line only boosts its own keywords. "Preferred" lines get a smaller boost. Boilerplate, the
  employer's name, the bare job title and hiring-policy language are dropped.
* **Coverage matching:** lemma-based, with a suffix-stem safety net for words the small model leaves
  unlemmatized.
* **Bullet coaching:** detects action verbs (parsing the bullet as "I …" so the tagger sees a real sentence),
  numbers, weak openers ("responsible for") and length.
* **Letters:** the most relevant bullets (ranked against the posting) are rewritten into prose. The letter
  names the skills and credentials the posting asks for. Nothing is invented.

* **Interview coaching:** each S.T.A.R.S part is checked with spaCy: first-person verbs vs. "we" in the Action,
  numbers and outcome words in the Result, named skills (and the posting's keywords) in Skills, setting
  language, hedges and blame words. The assessment items live in `seek/data/assessment.json`. They are
  trait-keyed, with reverse-worded pairs for consistency and "never/always" items for realism. They are for
  practice and coaching, not psychological testing.

If the model isn't installed, the engine runs in *basic* mode (a blank English pipeline plus rules) and
`system.status` reports how to install the full model. The shell shows that on Home and Settings.

## Fair-chance features (justice outreach)

* **Review:** flags resume wording that leads with the setting instead of the skill ("Correctional Facility",
  "inmates", legal status). It suggests skill-first alternatives and job-title ideas taken from the entry's
  own content.
* **Gap detection:** looks across jobs, training, education and volunteering, and suggests ways to fill gaps.
* **Posting signals:** fair-chance language, background checks, drug screens, driving-record checks and
  exclusion language ("no felonies") are surfaced for staff.
* **Letters:** an optional growth paragraph about readiness. It never mentions a record.
* **Guardrails:** all of these are suggestions. Nothing is changed automatically, and the guidance is explicit:
  never misstate history, and answer direct questions honestly.

## UI

The look follows the QWidget-FancyUI demo shipped in `QWidget-FancyUI/`. That library is Windows-only
(DWM Mica/Acrylic), so SEEK re-implements the design portably in plain Qt 6 Widgets:

* the frameless window, custom title bar and collapsible icon sidebar with its accent indicator
* the rounded content area and the hero banner with glass cards
* the FancyUI light/dark palette (`#F3F3F3` / `#202020`)
* Bootstrap icons tinted per theme

Set `SEEK_NATIVE_FRAME=1`, or use the Settings toggle, to use the OS title bar instead.

`SEEK_AUTOSHOT=<folder>` is a developer aid. It saves a PNG of every page in both themes and exits. The images
in `docs/screenshots` were made this way.
