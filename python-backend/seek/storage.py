"""Local, durable storage for SEEK.

Everything stays on the machine: participant data from an outreach program is
sensitive, so nothing is synced or uploaded. Records are JSON documents written
atomically (write temp file, then rename) so a crash never leaves half a file.
"""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator

_LOCK = threading.RLock()
_SAFE_ID = re.compile(r"[A-Za-z0-9_-]{1,64}")  # used with fullmatch: "$" would accept a trailing newline
# Windows maps these names to devices whatever the extension ("CON.json" is the console).
_RESERVED = re.compile(r"(con|prn|aux|nul|com[0-9]|lpt[0-9])", re.I)


class NotFound(KeyError):
    """A record the caller asked for doesn't exist (the bridge reports it as ``not_found``)."""


def safe_id(value: str) -> bool:
    return bool(_SAFE_ID.fullmatch(value or "")) and not _RESERVED.fullmatch(value)


def as_list(value: Any) -> list[str]:
    """A list-of-strings parameter from the shell. A lone string is one item, not a list of letters."""
    if value is None:
        return []
    if isinstance(value, str):
        return [value] if value.strip() else []
    if isinstance(value, (list, tuple)):
        return [str(v) for v in value if v is not None and str(v).strip()]
    raise ValueError("expected a list of strings")


_CONTROL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def as_text(value: Any) -> str:
    """A text parameter from the shell; None means empty. Control characters pasted from Word or PowerPoint
    (e.g. \\x0b soft line breaks) become spaces: they are invisible, and .docx export refuses them."""
    return "" if value is None else _CONTROL.sub(" ", str(value))


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def new_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:12]}"


def default_data_dir() -> Path:
    """Per-user app data folder: %APPDATA%/SEEK, ~/Library/..., or XDG."""
    override = os.environ.get("SEEK_DATA_DIR")
    if override:
        return Path(override).expanduser()
    if sys.platform.startswith("win"):
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "SEEK"


class Store:
    """A folder of JSON documents grouped into collections (sub-folders)."""

    def __init__(self, root: Path | str | None = None) -> None:
        self.root = Path(root) if root else default_data_dir()
        self.root.mkdir(parents=True, exist_ok=True)

    # -- paths ---------------------------------------------------------------
    def _collection_dir(self, collection: str) -> Path:
        if not safe_id(collection):
            raise ValueError(f"invalid collection name: {collection!r}")
        path = self.root / collection
        path.mkdir(parents=True, exist_ok=True)
        return path

    def _doc_path(self, collection: str, doc_id: str) -> Path:
        # Ids come from the shell; never let one escape the collection folder.
        if not safe_id(doc_id or ""):
            raise ValueError(f"invalid id: {doc_id!r}")
        return self._collection_dir(collection) / f"{doc_id}.json"

    # -- documents -----------------------------------------------------------
    def get(self, collection: str, doc_id: str) -> dict[str, Any] | None:
        path = self._doc_path(collection, doc_id)
        if not path.exists():
            return None
        try:
            with path.open("r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"The saved {collection} record {doc_id} is damaged and can't be read.") from exc
        return doc if isinstance(doc, dict) else None

    def put(self, collection: str, doc_id: str, doc: dict[str, Any]) -> dict[str, Any]:
        path = self._doc_path(collection, doc_id)
        with _LOCK:
            # ".tmp" (not ".json"), so a file left by a crash is never read back as a record.
            fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-", suffix=".tmp")
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as fh:
                    json.dump(doc, fh, indent=2, ensure_ascii=False)
                    fh.flush()
                    os.fsync(fh.fileno())  # the rename must never publish an empty file after a power loss
                os.replace(tmp, path)
            except BaseException:
                Path(tmp).unlink(missing_ok=True)
                raise
        return doc

    def delete(self, collection: str, doc_id: str) -> bool:
        path = self._doc_path(collection, doc_id)
        with _LOCK:
            if path.exists():
                path.unlink()
                return True
        return False

    def all(self, collection: str) -> Iterator[dict[str, Any]]:
        for path in sorted(self._collection_dir(collection).glob("*.json")):
            if path.name.startswith("."):  # temp files from older versions
                continue
            try:
                with path.open("r", encoding="utf-8") as fh:
                    doc = json.load(fh)
            except (OSError, json.JSONDecodeError) as exc:
                print(f"[storage] skipping unreadable {path.name}: {exc}", file=sys.stderr)
                continue
            if not isinstance(doc, dict):
                print(f"[storage] skipping {path.name}: not a record", file=sys.stderr)
                continue
            doc.setdefault("id", path.stem)  # the file name is the id
            if doc["id"] != path.stem:
                print(f"[storage] skipping {path.name}: id doesn't match the file name", file=sys.stderr)
                continue
            yield doc

    # -- settings (single document) -----------------------------------------
    def settings(self) -> dict[str, Any]:
        return self.get("settings", "app") or {}

    def update_settings(self, **changes: Any) -> dict[str, Any]:
        current = self.settings()
        current.update(changes)
        return self.put("settings", "app", current)

    # -- history (append-only JSONL) ----------------------------------------
    def append_history(self, kind: str, summary: str, **data: Any) -> dict[str, Any]:
        """Durable activity log; the canon requires history for bridge-fed workflows."""
        entry = {"id": new_id("evt"), "at": utc_now(), "kind": kind, "summary": summary, "data": data}
        with _LOCK:
            with (self.root / "history.jsonl").open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(entry, ensure_ascii=False) + "\n")
        return entry

    def history(self, limit: int = 200, kind: str | None = None) -> list[dict[str, Any]]:
        path = self.root / "history.jsonl"
        if not path.exists():
            return []
        entries = []
        with path.open("r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if kind is None or entry.get("kind") == kind:
                    entries.append(entry)
        return list(reversed(entries))[:limit]
