# Build the SEEK shell with Qt 6 + MSVC and produce a runnable folder in .\dist
#   .\scripts\build_windows.ps1 -QtDir C:\Qt\6.7.2\msvc2019_64
param([Parameter(Mandatory = $true)][string]$QtDir)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
cmake -S "$root\shell" -B "$root\shell\build" -DCMAKE_PREFIX_PATH="$QtDir"
cmake --build "$root\shell\build" --config Release
cmake --install "$root\shell\build" --config Release --prefix "$root\dist"
& "$QtDir\bin\windeployqt.exe" --release --no-translations "$root\dist\SEEK.exe"
& (Join-Path $PSScriptRoot "setup_backend.ps1") -BackendDir "$root\dist\python-backend"
Write-Host "Done: $root\dist\SEEK.exe"
