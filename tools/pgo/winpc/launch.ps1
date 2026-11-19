param([switch]$Run, [int]$Mask = 3314, [string]$Build = 'build')   # -Build build-pgo2: the build with the extensive PGO profile (2026-11-19)
# Starts the max-fps build with the personal save, the material cache (mode 2, the low two bits) and every cpuopts bit on except 512
# (3314 = 2+16+32+64+128+1024+2048; 512, the shadow cascade budget, flickers the ground while panning and is left out). Nothing else is switched on: no control port, no bench counters, no profiler.
# -Mask 2 = the old play build's setting (material cache only), -Mask 3826 = with the shadow budget (512, known bad).
$ErrorActionPreference = 'Stop'
$wt   = 'X:\.Development\swr-wt\max-fps'
$data = 'X:\.Development\Projects\SavageWindRecomp'
$out  = 'C:\Users\derfa\swr-check\maxfps\run'
if (-not $Run) {
  if (Get-Process savagewind -ErrorAction SilentlyContinue) { throw 'A game is already running; leaving it alone.' }
  $action = New-ScheduledTaskAction -Execute (Get-Command pwsh.exe).Source -Argument "-NoProfile -WindowStyle Hidden -File `"$PSCommandPath`" -Run -Mask $Mask -Build $Build"
  $principal = New-ScheduledTaskPrincipal -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
  Register-ScheduledTask -TaskName 'SWR-MAXFPS-Play' -Action $action -Principal $principal -Settings (New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero)) -Force | Out-Null
  Start-ScheduledTask 'SWR-MAXFPS-Play'
  exit
}
Get-ChildItem Env:SAVAGEWIND_* | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$env:SAVAGEWIND_DATA_ROOT = $data
$env:SAVAGEWIND_VK_CPU_OPTS = [string]$Mask   # (the low two bits are the material cache mode; no separate STAGE_REUSE variable)
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
New-Item -ItemType Directory -Force $out | Out-Null
$log = "$out\launches.txt"
"$(Get-Date -Format s)  build=$wt\$Build\savagewind.exe  save-root=$data  cpuopts=$Mask" | Add-Content $log
$p = Start-Process "$wt\$Build\savagewind.exe" -WorkingDirectory $wt -PassThru -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
"$(Get-Date -Format s)  pid $($p.Id)" | Add-Content $log
$p.WaitForExit()
"$(Get-Date -Format s)  exit code $($p.ExitCode)" | Add-Content $log
