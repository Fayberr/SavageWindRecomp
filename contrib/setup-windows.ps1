# Run using PowerShell 7: pwsh -NoProfile -File contrib/setup-windows.ps1
# Run from the Windows checkout after source sync, never share Linux virtualenvs or build artifacts.
# This script builds and runs synthetic tests only; it does not launch the game.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use pwsh (PowerShell 7 or newer).' }
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path .git)) {
    # Syncthing excludes Git metadata. Attach it without overwriting synced source files.
    & git init
    if ($LASTEXITCODE) { throw 'git init failed' }
    & git remote add upstream https://github.com/Revivedx/SavageWindRecomp.git
    & git remote add origin https://github.com/Fayberr/SavageWindRecomp.git
    & git fetch origin setup/contribution-environment
    if ($LASTEXITCODE) { throw 'git fetch failed' }
    & git symbolic-ref HEAD refs/heads/setup/contribution-environment
    & git reset --mixed origin/setup/contribution-environment
    if ($LASTEXITCODE) { throw 'git reset failed' }
}
& git config core.autocrlf false
& git config core.eol lf

# Upstream installs MSYS2 UCRT64 and python.org Python, without changing GPU drivers.
& pwsh -NoProfile -File tools/setup_windows.ps1
if ($LASTEXITCODE) { throw 'Upstream setup failed' }
$bash = 'C:\msys64\usr\bin\bash.exe'
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
$env:SAVAGEWIND_CONTRIB_ROOT = $root
& $bash -lc 'cd "$(cygpath -u "$SAVAGEWIND_CONTRIB_ROOT")" && tools/venv/Scripts/python.exe -m pip install -r contrib/requirements.txt'
if ($LASTEXITCODE) { throw 'Python dependencies failed' }
& $bash -lc 'cd "$(cygpath -u "$SAVAGEWIND_CONTRIB_ROOT")" && export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH" && bash contrib/check.sh'
if ($LASTEXITCODE) { throw 'Source checks or build failed' }
Write-Host 'Runtime built in build/. Gameplay requires your own extracted Switch BotW 1.9.0 files.'
