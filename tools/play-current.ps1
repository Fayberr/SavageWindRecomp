# Deploy beside current-build.txt in the personal play launcher directory.
# Starts the selected SWR build with Fabian's personal save.
# Build to run: first line of current-build.txt (worktree folder containing build\savagewind.exe).
# Save used: the main checkout data root (early-plateau save). Edit $data below to change.
$here = Split-Path -Parent $PSCommandPath
$wt   = (Get-Content "$here\current-build.txt" -TotalCount 1).Trim()
$data = 'X:\.Development\Projects\SavageWindRecomp'
$exe  = "$wt\build\savagewind.exe"
if (-not (Test-Path $exe)) {
  Add-Type -AssemblyName PresentationFramework
  [void][System.Windows.MessageBox]::Show("Build not found: $exe", "SWR launcher")
  exit 1
}
# Build-specific opt-in, defaulting to the original renderer path.
$stageReuse = 0
$optionsPath = "$wt\build\play-options.json"
if (Test-Path $optionsPath) {
  $options = Get-Content $optionsPath -Raw | ConvertFrom-Json -ErrorAction Stop
  if ($options.stageReuse -notin @(0, 1, 2)) { throw "Invalid stageReuse in $optionsPath" }
  $stageReuse = [int]$options.stageReuse
}
$env:SAVAGEWIND_VK_STAGE_REUSE = [string]$stageReuse
$env:SAVAGEWIND_DATA_ROOT = $data
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
$log = "$here\run"; New-Item -ItemType Directory -Force $log | Out-Null
"$(Get-Date -Format s)  build=$exe  save-root=$data  stage-reuse=$stageReuse" | Add-Content "$here\last-launch.txt"
# Same start method as the verified test launcher (redirected stdio), waits so the exit code gets logged.
$p = Start-Process $exe -WorkingDirectory $wt -PassThru -RedirectStandardOutput "$log\stdout.log" -RedirectStandardError "$log\stderr.log"
$p.WaitForExit()
"$(Get-Date -Format s)  exit code $($p.ExitCode) (0x{0:X})" -f $p.ExitCode | Add-Content "$here\last-launch.txt"
