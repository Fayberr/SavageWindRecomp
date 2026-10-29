# Run using PowerShell 7: pwsh -NoProfile -File contrib/setup-windows.ps1
# Run from the Windows checkout after source sync, never share Linux virtualenvs or build artifacts.
# This script builds and runs synthetic tests only; it does not launch the game.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use pwsh (PowerShell 7 or newer).' }
$env:PYTHONUTF8 = '1'
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

# WindowsApps is often absent from the OpenSSH PATH. The archive fallback also works without winget.
$windowsApps = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps'
if (Test-Path $windowsApps) { $env:PATH = "$windowsApps;$env:PATH" }
if (-not (Test-Path C:\msys64\usr\bin\pacman.exe)) {
    if (Test-Path C:\msys64) { throw 'C:\msys64 exists but has no pacman; inspect it before installing.' }
    $cache = Join-Path $root 'build\contrib'
    New-Item -ItemType Directory -Force $cache | Out-Null
    $archive = Join-Path $cache 'msys2-base-x86_64-20260927.sfx.exe'
    Invoke-WebRequest 'https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.sfx.exe' -OutFile $archive
    $expected = 'ad336cccfda47758b5e15cda993fbba421115cb0b126697daef1ee4dfe37209f'
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $expected) { throw 'MSYS2 archive checksum mismatch' }
    $extract = Start-Process -FilePath $archive -ArgumentList '-y', '-oC:\' -Wait -PassThru -NoNewWindow
    if ($extract.ExitCode) { throw "MSYS2 extraction failed: $($extract.ExitCode)" }
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = '1'
    & C:\msys64\usr\bin\bash.exe -lc 'true'
    if ($LASTEXITCODE) { throw 'MSYS2 initialization failed' }
}

# Upstream installs MSYS2 UCRT64 and python.org Python, without changing GPU drivers.
& pwsh -NoProfile -File tools/setup_windows.ps1
if ($LASTEXITCODE) { throw 'Upstream setup failed' }
foreach ($tool in 'C:\msys64\ucrt64\bin\clang.exe', 'C:\msys64\usr\bin\make.exe') {
    if (-not (Test-Path $tool)) { throw "MSYS2 package installation is incomplete: $tool is missing. Inspect the pacman log and retry setup." }
}
$bash = 'C:\msys64\usr\bin\bash.exe'
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
$env:SAVAGEWIND_CONTRIB_ROOT = $root
& $bash -lc 'cd "$(cygpath -u "$SAVAGEWIND_CONTRIB_ROOT")" && tools/venv/Scripts/python.exe -m pip install -r contrib/requirements.txt'
if ($LASTEXITCODE) { throw 'Python dependencies failed' }
& $bash -lc 'cd "$(cygpath -u "$SAVAGEWIND_CONTRIB_ROOT")" && export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH" && bash contrib/check.sh'
if ($LASTEXITCODE) { throw 'Source checks or build failed' }
Write-Host 'Runtime built in build/. Gameplay requires your own extracted Switch BotW 1.9.0 files.'
