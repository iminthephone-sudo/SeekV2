# Create python-backend\.venv, install the engine pack's requirements and the spaCy model.
# The shell finds this venv automatically.
#   .\scripts\setup_backend.ps1                          # source tree (development)
#   .\scripts\setup_backend.ps1 -BackendDir dist\python-backend   # packaged folder
param([string]$BackendDir = (Join-Path $PSScriptRoot "..\python-backend"))
$ErrorActionPreference = "Stop"
Set-Location $BackendDir
$py = if ($env:PYTHON) { $env:PYTHON } else { "py" }
& $py -3 -m venv .venv
& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install -r requirements.txt
$model = if ($env:SEEK_SPACY_MODEL) { $env:SEEK_SPACY_MODEL } else { "en_core_web_sm" }
& .\.venv\Scripts\python.exe -m spacy download $model
& .\.venv\Scripts\python.exe seek_cpp_bridge.py --no-qt --call system.status
Write-Host "Engine ready."
