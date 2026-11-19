param([Parameter(Mandatory)][ValidateSet('build','smoke','train','merge','use')][string]$Stage, [string]$Profdata = '', [string]$Dir = 'build-pgo2', [switch]$Run)
# Stages of the extensive PGO training on branch perf/max-fps (see tools/pgo/train.py). Each runs detached as an interactive scheduled task
# (SWR-PGO-<stage>), status in $out\status.txt, logs in $out\<stage>.log. Never touches the running game or the player's save and settings.
$ErrorActionPreference = 'Stop'
$main = 'X:\.Development\Projects\SavageWindRecomp'
$wt   = 'X:\.Development\swr-wt\max-fps'
$root = 'X:\.Development\swr-wt\max-fps-pgo'
$out  = 'C:\Users\derfa\swr-check\maxfps\pgo'
$py   = "$main\tools\venv\Scripts\python.exe"
$want = '9d70702'
New-Item -ItemType Directory -Force $out | Out-Null
if (-not $Run) {
  $argText = "-NoProfile -File `"$PSCommandPath`" -Stage $Stage -Dir $Dir -Run" + $(if ($Profdata) { " -Profdata `"$Profdata`"" } else { '' })
  $action = New-ScheduledTaskAction -Execute (Get-Command pwsh.exe).Source -Argument $argText
  $principal = New-ScheduledTaskPrincipal -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
  Register-ScheduledTask -TaskName "SWR-PGO-$Stage" -Action $action -Principal $principal -Settings (New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero)) -Force | Out-Null
  Start-ScheduledTask "SWR-PGO-$Stage"
  exit
}
try {
  "RUNNING $Stage $(Get-Date -Format s)" | Set-Content "$out\status.txt"
  $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;C:\Program Files\Git\cmd;$env:PATH"   # (the game's runtime DLLs are in the msys UCRT64 folder)
  $env:MSYSTEM = 'UCRT64'; $env:CHERE_INVOKING = '1'; $env:PYTHONUTF8 = '1'
  switch ($Stage) {
    'build' {
      git -C $main fetch origin perf/max-fps 2>&1 | Add-Content "$out\build.log"
      if ($LASTEXITCODE) { throw "fetch failed: $LASTEXITCODE" }
      git -C $wt merge --ff-only origin/perf/max-fps 2>&1 | Add-Content "$out\build.log"
      if ($LASTEXITCODE) { throw "fast-forward failed: $LASTEXITCODE" }
      $head = (git -C $wt rev-parse --short HEAD).Trim()
      "worktree at $head" | Add-Content "$out\build.log"
      if (-not $head.StartsWith($want)) { throw "worktree is at $head, expected $want" }
      & 'C:\msys64\usr\bin\bash.exe' -l 'C:/Users/derfa/swr-check/maxfps/pgo/build-gen.sh'
      if ($LASTEXITCODE) { throw "instrumented build failed: $LASTEXITCODE (see build-gen.log)" }
      New-Item -ItemType Directory -Force "$wt\build-gen" | Out-Null
      foreach ($f in 'swgame.dll','swgame.dll.id','nvngx_dlss.dll','NVIDIA-DLSS-LICENSE.txt') { if (Test-Path "$wt\build\$f") { Copy-Item "$wt\build\$f" "$wt\build-gen" -Force } }
      # the player's data root stays untouched: a data root of its own, a copy of the scratch save, the player's settings as they are now
      foreach ($d in 'data\build','data\cache','profiles','runs') { New-Item -ItemType Directory -Force "$root\$d" | Out-Null }
      if (-not (Test-Path "$root\data\extracted")) { New-Item -ItemType Junction -Path "$root\data\extracted" -Target "$main\extracted" | Out-Null }
      if (Test-Path "$root\save") { Remove-Item "$root\save" -Recurse -Force }
      Copy-Item 'X:\.Development\swr-pc-mouse\scratch\save' "$root\save" -Recurse
      Copy-Item "$main\savagewind.ini" "$root\base.ini" -Force
      "save copy: $([math]::Round((Get-ChildItem "$root\save" -Recurse -File | Measure-Object Length -Sum).Sum/1MB,1)) MB" | Add-Content "$out\build.log"
      (Get-FileHash "$wt\build-gen\savagewind.exe").Hash | Set-Content "$out\gen.sha256"
    }
    { $_ -in 'smoke','train' } {
      $smoke = $Stage -eq 'smoke'
      $tag = if ($smoke) { 'smoke' } else { 'train' }
      $prof = "$root\profiles-$tag"
      if (Test-Path $prof) { Remove-Item $prof -Recurse -Force }
      New-Item -ItemType Directory -Force $prof | Out-Null
      if (Test-Path "$root\runs-$tag") { Remove-Item "$root\runs-$tag" -Recurse -Force }
      Remove-Item "$root\data\cache\*" -Recurse -Force -ErrorAction SilentlyContinue   # a cold shader cache for the first boot
      $env:LLVM_PROFILE_FILE = "$prof\rt-%p.profraw"
      $extra = if ($smoke) { @('--quick','--routes','plateau','--configs','play') } else { @() }
      Set-Location $wt
      & $py tools\pgo\train.py --exe "$wt\build-gen\savagewind.exe" --tree $wt --data-root "$root\data" --save "$root\save" --ini "$root\base.ini" `
        --out "$root\runs-$tag" --profiles $prof --env SAVAGEWIND_VK_CPU_OPTS=3314 @extra *> "$out\$Stage.log"
      if ($LASTEXITCODE) { throw "training reported boots without a profile (see $Stage.log)" }
    }
    'merge' {
      $files = @(Get-ChildItem "$root\profiles-train" -Filter *.profraw -File | Where-Object { $_.Length -gt 0 } | Select-Object -ExpandProperty FullName)
      "$($files.Count) non-empty profraw files" | Set-Content "$out\merge.log"
      if (-not $files.Count) { throw 'no profile to merge' }
      & 'C:\msys64\ucrt64\bin\llvm-profdata.exe' merge -o "$out\extensive.profdata" @files *>> "$out\merge.log"
      if ($LASTEXITCODE) { throw 'merge failed' }
      & 'C:\msys64\ucrt64\bin\llvm-profdata.exe' show "$out\extensive.profdata" *>> "$out\merge.log"
    }
    'use' {
      $prof = ($Profdata -replace '^([A-Za-z]):', '/$1') -replace '\\', '/'
      & 'C:\msys64\usr\bin\bash.exe' -l 'C:/Users/derfa/swr-check/maxfps/pgo/build-use.sh' $prof $Dir
      if ($LASTEXITCODE) { throw "PGO=use build failed: $LASTEXITCODE (see build-use-$Dir.log)" }
      New-Item -ItemType Directory -Force "$wt\$Dir" | Out-Null
      foreach ($f in 'swgame.dll','swgame.dll.id','nvngx_dlss.dll','NVIDIA-DLSS-LICENSE.txt') { if (Test-Path "$wt\build\$f") { Copy-Item "$wt\build\$f" "$wt\$Dir" -Force } }
      (Get-FileHash "$wt\$Dir\savagewind.exe").Hash | Set-Content "$out\$Dir.sha256"
    }
  }
  "EXIT 0 $Stage $(Get-Date -Format s)" | Set-Content "$out\status.txt"
} catch { $_ | Out-String | Add-Content "$out\error.txt"; "FAILED $Stage $(Get-Date -Format s)" | Set-Content "$out\status.txt"; exit 1 }
