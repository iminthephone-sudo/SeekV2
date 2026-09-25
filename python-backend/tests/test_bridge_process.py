"""Drive the real bridge process exactly the way the C++ shell does."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

BRIDGE = Path(__file__).resolve().parent.parent / "seek_cpp_bridge.py"


def _packets(stdout: str) -> list[tuple[str, dict]]:
    out = []
    for line in stdout.splitlines():
        prefix, _, body = line.partition(":")
        assert prefix in ("SEEK_JSON", "SEEK_PROGRESS"), f"non-protocol line on stdout: {line!r}"
        out.append((prefix, json.loads(body)))
    return out


def test_stdio_session(tmp_path):
    requests = [
        {"id": "1", "method": "system.ping"},
        {"id": "2", "method": "profile.create", "params": {"name": "Test", "participant": "P"}},
        {"id": "3", "method": "profile.list"},
        {"id": "4", "method": "nope.nothing"},
        {"id": "5", "method": "job.fetch", "params": {"url": "ftp://example.org"}},
    ]
    stdin = "\n".join(json.dumps(r) for r in requests) + "\nnot json\n" + \
        json.dumps({"id": "9", "method": "system.shutdown"}) + "\n"
    proc = subprocess.run([sys.executable, str(BRIDGE), "--no-qt", "--data-dir", str(tmp_path)],
                          input=stdin, capture_output=True, text=True, timeout=120, encoding="utf-8")
    assert proc.returncode == 0, proc.stderr
    packets = [p for kind, p in _packets(proc.stdout) if kind == "SEEK_JSON"]
    ready = packets[0]
    assert ready["event"] == "ready" and ready["result"]["protocol"] == 1
    by_id = {p.get("id"): p for p in packets[1:]}
    assert by_id["1"]["result"]["pong"] is True
    assert by_id["3"]["result"][0]["name"] == "Test"
    assert by_id["4"]["error"]["code"] == "unknown_method"
    assert by_id["5"]["error"]["code"] == "fetch_failed"
    assert by_id[None]["error"]["code"] == "bad_json"
    assert by_id["9"]["result"]["bye"] is True
    assert "[bridge] starting" in proc.stderr


def test_single_call_cli(tmp_path):
    proc = subprocess.run([sys.executable, str(BRIDGE), "--no-qt", "--data-dir", str(tmp_path), "--call",
                           "job.keywords", "--params", json.dumps({"text": "Must have CDL Class A and forklift experience."})],
                          capture_output=True, text=True, timeout=120, encoding="utf-8")
    (_, packet), = _packets(proc.stdout)
    terms = [k["term"] for k in packet["result"]]
    assert "CDL Class A" in terms and "forklift" in terms
