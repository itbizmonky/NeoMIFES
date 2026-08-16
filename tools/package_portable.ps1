<#
.SYNOPSIS
    Builds a no-install-required "Portable Zip" distribution of NeoMIFES
    (WI-13, build_plan.md 6 "Portable Zip 配布").

.DESCRIPTION
    Release build's MSVC_RUNTIME_LIBRARY is CMake's default (dynamic /MD,
    see WI-13 plan Context) - NeoMIFES.exe therefore depends on the VC++
    redistributable DLLs (MSVCP140.dll / VCRUNTIME140.dll /
    VCRUNTIME140_1.dll), confirmed via `dumpbin /dependents` rather than
    assumed. Everything else NeoMIFES.exe links against (USER32, d2d1,
    DWrite, d3d11, COMCTL32, IMM32, the api-ms-win-crt-*.dll Universal CRT
    forwarders, etc.) ships as part of Windows 10 1607+/Windows 11 itself
    and does not need bundling. This script copies just those 3 CRT DLLs
    next to the exe ("app-local deployment", a standard, installer-free
    way to satisfy the CRT dependency - no admin rights, no system-wide
    install, no touching this project's own CMake/ABI configuration).

    Does NOT rebuild or re-sign - run cmake --build --preset release and
    tools\sign_release_binary.ps1 first if you want a fresh/signed binary
    in the package.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$exePath  = Join-Path $repoRoot 'build\release\src\app\NeoMIFES.exe'

if (-not (Test-Path $exePath)) {
    throw "NeoMIFES.exe not found at $exePath - build the release preset first."
}

# ---- Version, from CMakeLists.txt's project(NeoMIFES VERSION x.y.z ...) --
$cmakeListsText = Get-Content (Join-Path $repoRoot 'CMakeLists.txt') -Raw
if ($cmakeListsText -notmatch 'project\s*\(\s*NeoMIFES\s+VERSION\s+([\d\.]+)') {
    throw "Could not find 'project(NeoMIFES VERSION x.y.z ...)' in CMakeLists.txt"
}
$version = $Matches[1]
Write-Output "Packaging NeoMIFES version $version"

# ---- Locate the VC++ redistributable CRT DLLs (app-local deployment) ----
$crtDllNames = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')
$redistRoot  = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC'
$crtDir = Get-ChildItem -Path $redistRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC*.CRT' } |
    ForEach-Object { Get-Item $_ -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $crtDir) {
    throw "Could not locate a VC++ redistributable x64 CRT folder under $redistRoot - is the Visual Studio C++ toolset installed?"
}
Write-Output "Using CRT redist folder: $($crtDir.FullName)"

foreach ($dll in $crtDllNames) {
    if (-not (Test-Path (Join-Path $crtDir.FullName $dll))) {
        throw "Expected CRT DLL not found: $dll under $($crtDir.FullName)"
    }
}

# ---- Assemble the portable folder ----
$distRoot   = Join-Path $repoRoot 'dist'
$packageDir = Join-Path $distRoot "NeoMIFES-portable-$version"
if (Test-Path $packageDir) { Remove-Item $packageDir -Recurse -Force }
New-Item -ItemType Directory -Path $packageDir | Out-Null

Copy-Item -Path $exePath -Destination $packageDir
foreach ($dll in $crtDllNames) {
    Copy-Item -Path (Join-Path $crtDir.FullName $dll) -Destination $packageDir
}

@"
NeoMIFES $version - Portable
=============================

No installation required. Run NeoMIFES.exe directly from this folder (or
any folder you copy it to - keep the .dll files alongside the .exe).

Settings, key bindings, and autosave data are stored per-user under
%APPDATA%\NeoMIFES\ - nothing is written next to the exe itself, so this
folder can be run from a read-only location (e.g. a USB drive) if desired.

See docs/user/keybindings.md in the source repository for a key binding
reference.
"@ | Out-File -FilePath (Join-Path $packageDir 'README.txt') -Encoding utf8

# ---- Zip it ----
$zipPath = Join-Path $distRoot "NeoMIFES-portable-$version.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $packageDir -DestinationPath $zipPath

$zipSizeMB = [Math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Output "Created: $zipPath ($zipSizeMB MB)"
