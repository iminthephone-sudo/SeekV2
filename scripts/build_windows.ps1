# Build the SEEK shell with Qt 6 + MSVC and produce a runnable folder in .\dist
#
#   .\scripts\build_windows.ps1                               # finds Qt in C:\Qt or D:\Qt by itself
#   .\scripts\build_windows.ps1 -QtDir D:\Qt\6.10.2           # a Qt version folder: its MSVC kit is used
#   .\scripts\build_windows.ps1 -QtDir D:\Qt\6.10.2\msvc2022_64
#   .\scripts\build_windows.ps1 -ShowQt                       # only show which Qt kit would be used
#   .\scripts\build_windows.ps1 -Clean                        # start from an empty build folder
#   .\scripts\build_windows.ps1 -NoWebEngine                  # build without the built-in browser on purpose
#
# Needs: Visual Studio 2022 (C++ workload), Qt 6 with the "MSVC 2022 64-bit" kit and, for the built-in
# browser (Indeed import, job search, sign-ins), the "Qt WebEngine" component. Python 3.10+ for the engine.
param(
    [string]$QtDir = "",
    [switch]$ShowQt,
    [switch]$Clean,
    [switch]$NoWebEngine
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# -- finding Qt ---------------------------------------------------------------------------------------
function Get-DeployTool([string]$kit) {
    foreach ($name in @("windeployqt.exe", "windeployqt6.exe")) {
        $tool = Join-Path (Join-Path $kit "bin") $name
        if (Test-Path $tool) { return $tool }
    }
    return $null
}

function Get-VersionOf([string]$name) {
    $v = $null
    if ([version]::TryParse($name, [ref]$v)) { return $v }
    return [version]"0.0"
}

# Every Qt kit (a folder with bin\windeployqt.exe) at, or up to two levels under, $dir.
# $dir may be a kit (D:\Qt\6.10.2\msvc2022_64), a version folder (D:\Qt\6.10.2) or the Qt root (D:\Qt).
function Get-QtKits([string]$dir) {
    $kits = @()
    if (-not (Test-Path $dir)) { return $kits }
    if (Get-DeployTool $dir) {
        return @([pscustomobject]@{ Path = (Resolve-Path $dir).Path; Name = (Split-Path $dir -Leaf)
                                    Version = Get-VersionOf (Split-Path (Split-Path $dir -Parent) -Leaf) })
    }
    foreach ($child in Get-ChildItem $dir -Directory -ErrorAction SilentlyContinue) {
        if (Get-DeployTool $child.FullName) {
            $kits += [pscustomobject]@{ Path = $child.FullName; Name = $child.Name; Version = Get-VersionOf (Split-Path $dir -Leaf) }
            continue
        }
        foreach ($kit in Get-ChildItem $child.FullName -Directory -ErrorAction SilentlyContinue) {
            if (Get-DeployTool $kit.FullName) {
                $kits += [pscustomobject]@{ Path = $kit.FullName; Name = $kit.Name; Version = Get-VersionOf $child.Name }
            }
        }
    }
    return $kits
}

function Select-QtKit([string]$dir) {
    $searched = @()
    if ($dir) { $searched = @($dir) }
    else {
        if ($env:QTDIR) { $searched += $env:QTDIR }
        $searched += @("C:\Qt", "D:\Qt")
        if ($env:USERPROFILE) { $searched += (Join-Path $env:USERPROFILE "Qt") }
    }
    $kits = @()
    foreach ($d in $searched) { if ($d) { $kits += Get-QtKits $d } }
    $arm = $env:PROCESSOR_ARCHITECTURE -eq "ARM64"
    # SEEK builds with MSVC (Qt WebEngine is only available for MSVC kits): 64-bit, the machine's architecture.
    $msvc = @($kits | Where-Object { $_.Name -like "msvc*" -and (($_.Name -like "*arm64*") -eq $arm) })
    if ($msvc.Count -eq 0) {
        $found = ($kits | ForEach-Object { "  " + $_.Path }) -join "`n"
        if (-not $found) { $found = "  (no Qt kits found)" }
        throw ("No Qt MSVC kit found in: " + ($searched -join ", ") + "`nKits found:`n$found`n" +
               "Install the 'MSVC 2022 64-bit' kit (and 'Qt WebEngine') with the Qt Maintenance Tool, or pass " +
               "-QtDir with its folder, e.g. -QtDir D:\Qt\6.10.2\msvc2022_64")
    }
    # Newest Qt first; then the newest compiler (msvc2022 before msvc2019).
    return @($msvc | Sort-Object -Property @{ Expression = { $_.Version }; Descending = $true },
                                            @{ Expression = { $_.Name }; Descending = $true })[0]
}

# Run a program and stop if it fails ($ErrorActionPreference doesn't cover programs, only PowerShell commands).
function Invoke-Step([string]$what, [scriptblock]$command) {
    Write-Host "==> $what" -ForegroundColor Cyan
    & $command
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE) - see the messages above." }
}

$kit = Select-QtKit $QtDir
$deploy = Get-DeployTool $kit.Path
$qtCmake = Join-Path (Join-Path $kit.Path "lib\cmake") "Qt6"
Write-Host "Qt kit:          $($kit.Path)"

# The built-in browser needs Qt WebEngine *and* the Qt modules it depends on, all the same version as Qt.
# (The Maintenance Tool lists WebChannel and Positioning separately; without them WebEngine can't load.)
function Get-ModuleVersion([string]$kitPath, [string]$module) {
    $dir = Join-Path (Join-Path $kitPath "lib\cmake") "Qt6$module"
    foreach ($name in @("Qt6$($module)ConfigVersionImpl.cmake", "Qt6$($module)ConfigVersion.cmake")) {
        $file = Join-Path $dir $name
        if (Test-Path $file) {
            $m = Select-String -Path $file -Pattern 'set\(PACKAGE_VERSION "([0-9.]+)"\)' | Select-Object -First 1
            if ($m) { return $m.Matches[0].Groups[1].Value }
        }
    }
    if (Test-Path $dir) { return "?" }
    return ""
}
$qtVersion = Get-ModuleVersion $kit.Path "Core"
$webProblems = @()
foreach ($module in @("WebEngineWidgets", "WebEngineCore", "WebChannel", "Positioning", "Quick", "QuickWidgets", "Qml",
                      "PrintSupport", "Network")) {
    $v = Get-ModuleVersion $kit.Path $module
    if (-not $v) { $webProblems += "Qt $module is not installed" }
    elseif ($v -ne "?" -and $qtVersion -and $v -ne $qtVersion) { $webProblems += "Qt $module is version $v, but Qt is $qtVersion" }
}
if ($webProblems.Count -eq 0) {
    Write-Host "Qt WebEngine:    installed with everything it needs (Qt $qtVersion) - the built-in browser will be on"
} else {
    Write-Warning ("The built-in browser (Indeed import, job search, LinkedIn/Indeed sign-ins) can't be built with this kit:`n  " +
                   ($webProblems -join "`n  ") + "`nFix: Qt Maintenance Tool > Add or remove components > Qt $qtVersion > " +
                   "tick 'Qt WebEngine', 'Qt WebChannel' and 'Qt Positioning' (under Additional Libraries or Extensions, " +
                   "all version $qtVersion), then build again with -Clean.")
}
if ($ShowQt) { return }
if ($webProblems.Count -gt 0 -and -not $NoWebEngine) {
    throw "Stopping so SEEK isn't built without its browser by accident. Fix the list above, or add -NoWebEngine to build without it."
}

# -- tools ----------------------------------------------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    # The Qt installer ships CMake under <Qt root>\Tools\CMake_64\bin.
    $qtRoot = Split-Path (Split-Path $kit.Path -Parent) -Parent
    $bundled = Join-Path $qtRoot "Tools\CMake_64\bin"
    if (Test-Path (Join-Path $bundled "cmake.exe")) { $env:PATH = "$bundled;$env:PATH" }
    else { throw "CMake not found. Install it (https://cmake.org/download/) or the Qt Maintenance Tool's CMake, or run from a 'Developer PowerShell for VS 2022'." }
}

# -- build --------------------------------------------------------------------------------------------
# Which Qt a configured build folder really uses. Qt6Core_DIR is set by find_package itself (Qt6_DIR can be
# "UNINITIALIZED" when it came from the command line). Returns a normalized path, or "" if not configured.
function Get-UsedQtCore([string]$cacheFile) {
    if (-not (Test-Path $cacheFile)) { return "" }
    $line = Select-String -Path $cacheFile -Pattern "^Qt6Core_DIR:[A-Z]+=(.*)$" | Select-Object -First 1
    if (-not $line) { return "" }
    return $line.Matches[0].Groups[1].Value.Replace("/", "\").TrimEnd("\")
}

$build = Join-Path $root "shell\build"
$cache = Join-Path $build "CMakeCache.txt"
$expectedCore = (Join-Path (Join-Path $kit.Path "lib\cmake") "Qt6Core").Replace("/", "\").TrimEnd("\")
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
elseif (Test-Path $cache) {
    # A build folder configured for another Qt keeps that Qt in its cache: start it fresh.
    $cachedCore = Get-UsedQtCore $cache
    if ($cachedCore -and ($cachedCore -ne $expectedCore)) {
        Write-Host "Build folder was set up for a different Qt ($cachedCore); starting it fresh."
        Remove-Item $build -Recurse -Force
    }
}
$dist = Join-Path $root "dist"
# Forward slashes: CMake reads backslashes in -D values as escapes in some places.
$kitCm = $kit.Path.Replace("\", "/")
$qtCmakeCm = $qtCmake.Replace("\", "/")
$webFlag = if ($NoWebEngine) { "-DSEEK_WEBENGINE=OFF" } else { "-DSEEK_WEBENGINE=ON" }
Invoke-Step "Configure (CMake)" { cmake -S (Join-Path $root "shell") -B $build "-DCMAKE_PREFIX_PATH=$kitCm" "-DQt6_DIR=$qtCmakeCm" $webFlag }
# CMake quietly falls back to another Qt it can find (on PATH, in the registry) when the kit isn't usable.
$usedCore = Get-UsedQtCore $cache
if (-not $usedCore) {
    throw "CMake finished but didn't record which Qt it used (no Qt6Core_DIR in $cache). Run again with -Clean."
}
if ($usedCore -ne $expectedCore) {
    throw "CMake used a different Qt ($usedCore) instead of $expectedCore. Check that the kit is complete, or run with -Clean."
}
Write-Host "Using Qt from:   $usedCore"
# What CMake actually managed to load (the files being there isn't enough).
$built = Select-String -Path $cache -Pattern "^SEEK_WEBENGINE_BUILT:[A-Z]+=(.*)$" | Select-Object -First 1
$missing = Select-String -Path $cache -Pattern "^SEEK_WEBENGINE_MISSING:[A-Z]+=(.*)$" | Select-Object -First 1
if ($built -and $built.Matches[0].Groups[1].Value -eq "ON") {
    Write-Host "Built-in browser: ON" -ForegroundColor Green
} elseif (-not $NoWebEngine) {
    $list = if ($missing) { $missing.Matches[0].Groups[1].Value.Replace(";", ", ") } else { "" }
    throw ("CMake couldn't load Qt WebEngine, so the built-in browser would be OFF. Modules that failed to load: $list. " +
           "Scroll up to the 'Failed to find optional Qt component' warning for the details. Install the missing " +
           "modules for Qt $qtVersion with the Qt Maintenance Tool and run with -Clean, or add -NoWebEngine.")
}
Invoke-Step "Build (Release)" { cmake --build $build --config Release }
Invoke-Step "Install to dist" { cmake --install $build --config Release --prefix $dist }
Invoke-Step "Add the Qt runtime (windeployqt)" { & $deploy --release --no-translations (Join-Path $dist "SEEK.exe") }
Invoke-Step "Set up the Python engine" { & (Join-Path $PSScriptRoot "setup_backend.ps1") -BackendDir (Join-Path $dist "python-backend"); $global:LASTEXITCODE = 0 }
Write-Host "Done: $(Join-Path $dist 'SEEK.exe')" -ForegroundColor Green
