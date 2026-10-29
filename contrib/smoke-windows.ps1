# Verify the native launcher on the logged-in Windows desktop, without game files.
# The launcher writes its own screenshot and exits; only this temporary task is removed afterwards.
[CmdletBinding()]
param([switch]$InteractiveChild)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use pwsh (PowerShell 7 or newer).' }
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\contrib\windows-smoke'
$resultPath = Join-Path $out 'result.json'
New-Item -ItemType Directory -Force $out | Out-Null
if ($InteractiveChild) {
    $result = [ordered]@{ success = $false; error = $null; exitCode = $null; screenshot = (Join-Path $out 'launcher.png') }
    $process = $null
    try {
        $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
        $env:SAVAGEWIND_DATA_ROOT = Join-Path $out 'data'
        $env:SAVAGEWIND_SETTINGS = Join-Path $out 'savagewind.ini'
        $env:SAVAGEWIND_SAVE_DIR = Join-Path $out 'save'
        $env:SAVAGEWIND_LAUNCHER_SHOT = '30:setup'
        $env:SAVAGEWIND_SHOT_PATH = $result.screenshot
        $env:SAVAGEWIND_VK_VALIDATION = '1'
        $env:VK_ADD_LAYER_PATH = 'C:\msys64\ucrt64\bin'
        Remove-Item $result.screenshot -ErrorAction SilentlyContinue
        $process = Start-Process (Join-Path $root 'build\savagewind.exe') -ArgumentList '--config' -WorkingDirectory $root -PassThru `
            -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
        if (-not $process.WaitForExit(45000)) { throw 'Launcher did not finish within 45 seconds.' }
        $result.exitCode = $process.ExitCode
        if ($process.ExitCode -ne 0) { throw "Launcher exited with $($process.ExitCode)." }
        if (-not (Test-Path $result.screenshot)) { throw 'Launcher did not write its screenshot.' }
        $result.success = $true
    } catch {
        $result.error = $_.Exception.Message
    } finally {
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
        $result | ConvertTo-Json | Set-Content -Encoding utf8 $resultPath
    }
    if (-not $result.success) { exit 1 }
    exit 0
}
if (-not (Test-Path (Join-Path $root 'build\savagewind.exe'))) { throw 'Build the Windows runtime first.' }
if (-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -gt 0)) {
    throw 'A logged-in Windows desktop is required for the GPU smoke test.'
}
Remove-Item $resultPath -ErrorAction SilentlyContinue
$name = 'SavageWind-Smoke-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$action = New-ScheduledTaskAction -Execute (Get-Command pwsh).Source -Argument "-NoProfile -File `"$PSCommandPath`" -InteractiveChild" -WorkingDirectory $root
$principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings | Out-Null
try {
    Start-ScheduledTask -TaskName $name
    $deadline = (Get-Date).AddSeconds(75)
    while (-not (Test-Path $resultPath) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
    if (-not (Test-Path $resultPath)) { throw 'Interactive test produced no result within 75 seconds.' }
    $result = Get-Content -Raw $resultPath | ConvertFrom-Json
    $result | ConvertTo-Json
    if (-not $result.success) { throw $result.error }
} finally {
    Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $name -Confirm:$false
}
