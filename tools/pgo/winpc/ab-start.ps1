if (Get-Process savagewind -ErrorAction SilentlyContinue) { throw 'a game is running; leaving it alone' }
$a = New-ScheduledTaskAction -Execute (Get-Command pwsh.exe).Source -Argument '-NoProfile -WindowStyle Hidden -File C:\Users\derfa\swr-check\maxfps\pgo\ab.ps1'
$p = New-ScheduledTaskPrincipal -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName 'SWR-PGO-ab' -Action $a -Principal $p -Settings (New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero)) -Force | Out-Null
Start-ScheduledTask 'SWR-PGO-ab'
