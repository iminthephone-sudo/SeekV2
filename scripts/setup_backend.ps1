# Create python-backend\.venv, install the engine pack's requirements and the spaCy model.
# The shell finds this venv automatically.
#   .\scripts\setup_backend.ps1                                   # source tree (development)
#   .\scripts\setup_backend.ps1 -BackendDir dist\python-backend   # packaged folder
# Set $env:PYTHON to a python.exe to choose the interpreter.
param([string]$BackendDir = (Join-Path $PSScriptRoot "..\python-backend"))
$ErrorActionPreference = "Stop"

# Run a program and stop if it fails ($ErrorActionPreference doesn't cover programs, only PowerShell commands).
function Invoke-Step([string]$what, [scriptblock]$command) {
    Write-Host "==> $what" -ForegroundColor Cyan
    & $command
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE) - see the messages above." }
}

Set-Location $BackendDir
# The "py" launcher comes with the python.org installer; otherwise use whatever "python" is on PATH.
if ($env:PYTHON) { $py = @($env:PYTHON) }
elseif (Get-Command py -ErrorAction SilentlyContinue) { $py = @("py", "-3") }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = @("python") }
else { throw "Python 3.10+ not found. Install it from https://www.python.org/downloads/ (tick 'Add python.exe to PATH')." }
$exe = $py[0]
$pyArgs = @($py | Select-Object -Skip 1)

Invoke-Step "Create the engine's virtual environment" { & $exe @pyArgs -m venv .venv }
$venv = Join-Path (Get-Location) ".venv\Scripts\python.exe"
Invoke-Step "Update pip" { & $venv -m pip install --upgrade pip }
Invoke-Step "Install the engine's packages" { & $venv -m pip install -r requirements.txt }
$model = if ($env:SEEK_SPACY_MODEL) { $env:SEEK_SPACY_MODEL } else { "en_core_web_sm" }
Invoke-Step "Download the spaCy model ($model)" { & $venv -m spacy download $model }
Invoke-Step "Check the engine starts" { & $venv seek_cpp_bridge.py --no-qt --call system.status }
Write-Host "Engine ready." -ForegroundColor Green
