"""SEEK bridge contract: method registry + packet I/O.

Wire format (documented in docs/BRIDGE_CONTRACT.md):

  shell -> engine  (stdin, one JSON object per line)
      {"id": "7", "method": "profile.get", "params": {"profile_id": "prof_..."}}

  engine -> shell  (stdout, one packet per line)
      SEEK_JSON:{"id": "7", "ok": true, "result": {...}}
      SEEK_JSON:{"id": "7", "ok": false, "error": {"code": "not_found", "message": "..."}}
      SEEK_JSON:{"id": null, "event": "ready", "result": {...status...}}
      SEEK_PROGRESS:{"id": "7", "percent": 40, "message": "Downloading posting…"}

  stderr: free-form diagnostics only (the shell shows them under Settings → Logs).

Nothing else is ever written to stdout: ``sys.stdout`` is redirected to stderr
at start-up so a stray print() in any library cannot corrupt the stream.
"""

from __future__ import annotations

import inspect
import json
import sys
import threading
import traceback
from typing import Any, Callable, TextIO

from . import __version__
from .engines import fair_chance, resume
from .engines.cover_letter import TONES, CoverLetterEngine
from .engines.jobs import STATUSES, FetchError, JobEngine, normalize_url
from .engines.nlp import NLP, get_nlp
from .engines.interview import InterviewEngine
from .engines.optimizer import OptimizerEngine
from .engines.profiles import CONTACT_FIELDS, SECTION_FIELDS, TEMPLATES, ProfileEngine
from .storage import Store

JSON_PREFIX = "SEEK_JSON:"
PROGRESS_PREFIX = "SEEK_PROGRESS:"
PROTOCOL_VERSION = 1


class BridgeError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


class Emitter:
    """Serialises packets onto the protocol stream (thread-safe)."""

    def __init__(self, stream: TextIO) -> None:
        self.stream = stream
        self.lock = threading.Lock()

    def send(self, prefix: str, payload: dict[str, Any]) -> None:
        line = prefix + json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        with self.lock:
            self.stream.write(line + "\n")
            self.stream.flush()


class SeekService:
    def __init__(self, store: Store | None = None, nlp: NLP | None = None) -> None:
        self.store = store or Store()
        self.nlp = nlp or get_nlp()
        self.profiles = ProfileEngine(self.store)
        self.jobs = JobEngine(self.store, self.nlp)
        self.optimizer = OptimizerEngine(self.store, self.nlp, self.profiles)
        self.letters = CoverLetterEngine(self.store, self.nlp, self.profiles)
        self.interview = InterviewEngine(self.store, self.nlp)
        self.shutdown_requested = False
        self._progress: Callable[[int, str], None] = lambda pct, msg: None
        self.methods: dict[str, Callable[..., Any]] = {
            "system.ping": lambda: {"pong": True, "version": __version__},
            "system.status": self.status,
            "system.methods": lambda: sorted(self.methods),
            "system.shutdown": self._shutdown,
            "settings.get": self.store.settings,
            "settings.update": lambda changes: self.store.update_settings(**(changes or {})),
            "profile.schema": lambda: {"sections": SECTION_FIELDS, "contact": CONTACT_FIELDS, "templates": TEMPLATES},
            "profile.list": self.profiles.list,
            "profile.get": self.profiles.get,
            "profile.create": self.profiles.create,
            "profile.update": self.profiles.update,
            "profile.delete": self.profiles.delete,
            "profile.duplicate": self.profiles.duplicate,
            "profile.set_active": self.profiles.set_active,
            "profile.active": self.profiles.active,
            "resume.render": self.resume_render,
            "resume.export": self.resume_export,
            "resume.analyze": lambda profile_id: resume.analyze(self.profiles.get(profile_id), self.nlp),
            "fairchance.review": lambda profile_id, gap_months=9: fair_chance.review_profile(
                self.profiles.get(profile_id), int(gap_months)),
            "fairchance.guidance": lambda: fair_chance.GUIDANCE,
            "job.fetch": lambda url: self.jobs.fetch(url, self._progress),
            "job.from_html": self.jobs.from_html,
            "job.page_url": lambda url: normalize_url(url.strip()),
            "job.from_text": self.jobs.from_text,
            "job.list": self.jobs.list,
            "job.get": self.jobs.get,
            "job.update": self.jobs.update,
            "job.delete": self.jobs.delete,
            "job.statuses": lambda: STATUSES,
            "job.keywords": lambda text, top=30: [k.to_dict() for k in self.nlp.keywords(text, int(top))],
            "optimize.match": self.optimizer.match,
            "optimize.apply": self.optimizer.apply,
            "letter.generate": self.letters.generate,
            "letter.tones": lambda: list(TONES),
            "letter.save": self.letters.save,
            "letter.get": self.letters.get,
            "letter.list": self.letters.list,
            "letter.delete": self.letters.delete,
            "letter.export": self.letters.export,
            "interview.stars_questions": self.interview.stars_questions,
            "interview.story_ideas": self.interview.story_ideas,
            "interview.stars_coach": self.interview.coach,
            "interview.assessment_items": self.interview.assessment_items,
            "interview.assessment_score": self.interview.score_assessment,
            "interview.saved": self.interview.saved,
            "interview.delete": self.interview.delete,
            "history.list": lambda limit=200, kind=None: self.store.history(int(limit), kind),
        }

    # -- handlers that need a little glue ------------------------------------------------
    def status(self) -> dict[str, Any]:
        return {
            "version": __version__,
            "protocol": PROTOCOL_VERSION,
            "python": sys.version.split()[0],
            "data_dir": str(self.store.root),
            "nlp": self.nlp.status(),
            "counts": {
                "profiles": sum(1 for _ in self.store.all("profiles")),
                "jobs": sum(1 for _ in self.store.all("jobs")),
                "letters": sum(1 for _ in self.store.all("letters")),
            },
            "active_profile": self.store.settings().get("active_profile", ""),
        }

    def resume_render(self, profile_id: str, format: str = "html", template: str | None = None) -> dict[str, Any]:
        profile = self.profiles.get(profile_id)
        fmt = format.lower()
        if fmt == "html":
            content = resume.render_html(profile, template)
        elif fmt in ("md", "markdown"):
            content = resume.render_markdown(profile)
        elif fmt in ("txt", "text"):
            content = resume.render_text(profile)
        else:
            raise ValueError(f"unsupported format: {format}")
        return {"format": fmt, "content": content, "template": template or profile["options"]["template"]}

    def resume_export(self, profile_id: str, format: str, path: str, template: str | None = None) -> dict[str, Any]:
        profile = self.profiles.get(profile_id)
        written = resume.export(profile, format, path, template)
        self.store.append_history("resume", f"Exported resume '{profile.get('name')}' as {format.upper()}",
                                  profile_id=profile_id, path=written)
        return {"path": written}

    def _shutdown(self) -> dict[str, Any]:
        self.shutdown_requested = True
        return {"bye": True}

    # -- dispatch ----------------------------------------------------------------------------
    def call(self, method: str, params: dict[str, Any] | None = None,
             progress: Callable[[int, str], None] | None = None) -> Any:
        handler = self.methods.get(method)
        if handler is None:
            raise BridgeError("unknown_method", f"Unknown method '{method}'.")
        params = params or {}
        if not isinstance(params, dict):
            raise BridgeError("invalid_params", "params must be an object")
        try:
            inspect.signature(handler).bind(**params)
        except TypeError as exc:
            raise BridgeError("invalid_params", f"{method}: {exc}") from exc
        self._progress = progress or (lambda pct, msg: None)
        try:
            return handler(**params)
        except BridgeError:
            raise
        except FetchError as exc:
            raise BridgeError("fetch_blocked" if exc.blocked else "fetch_failed", str(exc)) from exc
        except KeyError as exc:
            raise BridgeError("not_found", str(exc.args[0]) if exc.args else "not found") from exc
        except (ValueError, TypeError) as exc:
            raise BridgeError("invalid", str(exc)) from exc
        except RuntimeError as exc:
            raise BridgeError("engine_error", str(exc)) from exc
        finally:
            self._progress = lambda pct, msg: None


def handle_line(service: SeekService, emitter: Emitter, line: str) -> None:
    line = line.strip()
    if not line:
        return
    req_id = None
    try:
        request = json.loads(line)
        if not isinstance(request, dict):
            raise BridgeError("bad_request", "request must be a JSON object")
        req_id = request.get("id")
        method = request.get("method")
        if not isinstance(method, str):
            raise BridgeError("bad_request", "missing 'method'")

        def progress(percent: int, message: str) -> None:
            emitter.send(PROGRESS_PREFIX, {"id": req_id, "percent": int(percent), "message": message})

        result = service.call(method, request.get("params"), progress)
        emitter.send(JSON_PREFIX, {"id": req_id, "ok": True, "result": result})
    except json.JSONDecodeError as exc:
        emitter.send(JSON_PREFIX, {"id": None, "ok": False, "error": {"code": "bad_json", "message": str(exc)}})
    except BridgeError as exc:
        emitter.send(JSON_PREFIX, {"id": req_id, "ok": False, "error": {"code": exc.code, "message": exc.message}})
    except Exception as exc:  # noqa: BLE001 - last line of defence, keep the bridge alive
        traceback.print_exc(file=sys.stderr)
        emitter.send(JSON_PREFIX, {"id": req_id, "ok": False,
                                   "error": {"code": "internal", "message": f"{exc.__class__.__name__}: {exc}"}})


def serve(service: SeekService, emitter: Emitter, stdin: TextIO) -> int:
    emitter.send(JSON_PREFIX, {"id": None, "ok": True, "event": "ready", "result": service.status()})
    for line in stdin:
        handle_line(service, emitter, line)
        if service.shutdown_requested:
            break
    print("[bridge] stdin closed, exiting", file=sys.stderr)
    return 0
