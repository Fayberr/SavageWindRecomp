# Same-spot A/B of the builds with perf_routes (hidden window, 2560x1440, the player's settings, profiler and frame trace off).
# One boot per build and round, the order rotated each round so no build always runs first (thermals, cache).
$ErrorActionPreference = 'Continue'
$root = 'X:\.Development\swr-wt\max-fps-pgo'
$wt   = 'X:\.Development\swr-wt\max-fps'
$old  = 'X:\.Development\swr-wt\alpha6-stage-reuse'
$py   = 'X:\.Development\Projects\SavageWindRecomp\tools\venv\Scripts\python.exe'
$out  = "$root\ab"
$status = 'C:\Users\derfa\swr-check\maxfps\pgo\ab-status.txt'
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"; $env:PYTHONUTF8 = '1'
foreach ($k in (Get-ChildItem Env: | Where-Object { $_.Name -like 'SAVAGEWIND_*' })) { Remove-Item "Env:$($k.Name)" }
New-Item -ItemType Directory -Force $out | Out-Null
$configs = @(
  @{ n = 'old';    exe = "$old\build\savagewind.exe";          tree = $old; env = @('SAVAGEWIND_VK_STAGE_REUSE=2') },
  @{ n = 'cur';    exe = "$wt\build\savagewind.exe";           tree = $wt;  env = @('SAVAGEWIND_VK_CPU_OPTS=3314') },
  @{ n = 'new';    exe = "$wt\build-pgo2\savagewind.exe";      tree = $wt;  env = @('SAVAGEWIND_VK_CPU_OPTS=3314') },
  @{ n = 'new-m2'; exe = "$wt\build-pgo2\savagewind.exe";      tree = $wt;  env = @('SAVAGEWIND_VK_CPU_OPTS=2') }
)
Set-Location $wt
"RUNNING $(Get-Date -Format s)" | Set-Content $status
foreach ($round in 1..3) {
  $order = @(); foreach ($i in 0..($configs.Count - 1)) { $order += $configs[($i + $round - 1) % $configs.Count] }
  foreach ($c in $order) {
    "RUNNING round $round $($c.n) $(Get-Date -Format s)" | Set-Content $status
    "== round $round $($c.n) $(Get-Date -Format s)" | Add-Content "$out\ab.log"
    & $py tools\perf_routes.py run --tree $c.tree --save "$root\save" --ini "$root\base.ini" --out "$out\$($c.n)" --config $c.n `
      --runs 1 --first-run $round --routes plateau kakariko field hateno --exe $c.exe --data-root "$root\data" --sample 0 --no-trace `
      --boot 420 --port 7792 --env @($c.env) *>> "$out\ab.log"
    "exit $LASTEXITCODE" | Add-Content "$out\ab.log"
  }
}
"EXIT 0 $(Get-Date -Format s)" | Set-Content $status
