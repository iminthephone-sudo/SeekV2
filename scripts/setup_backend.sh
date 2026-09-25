#!/usr/bin/env bash
# Create python-backend/.venv, install the engine pack's requirements and the spaCy model.
# The shell finds this venv automatically.
set -euo pipefail
# Usage: scripts/setup_backend.sh [backend-dir]   (default: the source tree's python-backend)
cd "${1:-$(dirname "$0")/../python-backend}"
PY="${PYTHON:-python3}"
"$PY" -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m spacy download "${SEEK_SPACY_MODEL:-en_core_web_sm}"
.venv/bin/python seek_cpp_bridge.py --no-qt --call system.status
echo "Engine ready."
