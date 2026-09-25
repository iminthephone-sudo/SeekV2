#!/usr/bin/env python3
"""SEEK C++ -> Python bridge. The ONLY supported entry point for the shell.

The Qt shell launches:

    python python-backend/seek_cpp_bridge.py --no-qt [--data-dir DIR]

and then speaks the line protocol described in seek/bridge.py and
docs/BRIDGE_CONTRACT.md over stdin/stdout. Python never opens a window.

For scripting and tests, a single call can be made without the shell:

    python seek_cpp_bridge.py --no-qt --call profile.list
    python seek_cpp_bridge.py --no-qt --call job.from_text --params '{"text": "..."}'
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SEEK engine bridge (no UI).")
    parser.add_argument("--no-qt", action="store_true",
                        help="Engine-only mode. Always on: accepted for parity with the platform launch contract.")
    parser.add_argument("--data-dir", help="Override the app data folder (default: per-user app data).")
    parser.add_argument("--call", metavar="METHOD", help="Run one method, print one SEEK_JSON packet, exit.")
    parser.add_argument("--params", default="{}", help="JSON object of params for --call.")
    args = parser.parse_args(argv)

    # Protocol stream: keep the real stdout for packets, send everything else to stderr.
    protocol = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n", line_buffering=True)
    sys.stdout = sys.stderr
    stdin = io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8")

    if args.data_dir:
        os.environ["SEEK_DATA_DIR"] = args.data_dir

    from seek.bridge import Emitter, SeekService, handle_line, serve
    from seek.storage import Store

    emitter = Emitter(protocol)
    print(f"[bridge] starting; data dir = {Store().root}", file=sys.stderr)
    service = SeekService()
    print(f"[bridge] NLP mode = {service.nlp.mode} ({service.nlp.model_name})", file=sys.stderr)

    if args.call:
        try:
            params = json.loads(args.params)
        except json.JSONDecodeError as exc:
            print(f"--params is not valid JSON: {exc}", file=sys.stderr)
            return 2
        handle_line(service, emitter, json.dumps({"id": "cli", "method": args.call, "params": params}))
        return 0
    return serve(service, emitter, stdin)


if __name__ == "__main__":
    sys.exit(main())
