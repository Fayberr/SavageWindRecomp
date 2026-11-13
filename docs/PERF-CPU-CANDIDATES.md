# CPU candidates handoff

The experiment is stopped after both routes. The original play executable is
restored and the Aktueller Build shortcut target is unchanged. Candidates remain
opt-in on branch `perf/cpu-candidates-20261113`; none was selected for play.

## Results, 2026-11-13

Median of three 15-second windows per candidate and route, from one loaded game
per route. Renderer ms is `cpu_render_ms`: render-thread CPU time divided by
presented frames, not the wall time of `backend_execute`. FPS is independently
medianed. All 30 windows completed; no pipeline compiles were recorded in them.

| Candidate | Mask | Plateau renderer ms | Plateau FPS | Castle renderer ms | Castle FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Control | 0 | 10.035 | 92.55 | 10.285 | 86.58 |
| Precomputed binding order | 1 | 9.903 | 93.59 | 9.578 | 92.95 |
| Descriptor writes only on cache misses | 2 | 9.863 | 95.02 | 9.654 | 96.04 |
| Bounded driver-constant packing | 4 | 10.021 | 92.86 | 9.486 | 98.26 |
| All three | 7 | 10.005 | 91.79 | 8.999 | 104.88 |

Hardware: Ryzen 5700X3D, RTX 3080 Ti, NVIDIA 610.47, Windows. Source: `cb202ef`
plus these experiments, MSYS2 UCRT64 clang 23.1.4, PGO + ThinLTO. Output 1440p,
FSR1 Ultra Quality, FXAA, high shadows, medium draw distance, FG off; other
visual settings copied from the play INI. Benchmark FPS uncapped, VSync off.
Stack sampling and per-draw timers off; whole-list timing and frame tracing on.

**Limits:** control FPS ranged 86.02-93.49 on Plateau and 78.10-103.55 at Castle.
These medians include substantial temporal variation and do not establish causal
percentage gains, a 25% CPU reduction or consistent 100 FPS. All-three did not
improve Plateau's median FPS. The recorded run's order is in `live/run.log`;
the harness's paired-order correction was made afterward and was not rerun.
The optional `binding_writes` mask 3 was not performance-tested. No follow-up
matrix or new optimization was started after the stop instruction.

Evidence: `build/cpu-candidates/live/results.csv`, manifest, route screenshots and
traces; `live.log` on Windows, `live/run.log` and `live/summary.json` in the Linux
analysis copy. The preserved executable is `build/cpu-candidates/candidate/`
with its matching game DLL, ID and manifest. Generated artifacts stay uncommitted.

## Running the harness

From this branch in the Windows checkout, using `pwsh` and Python 3:

```powershell
$tree = (Get-Location).Path
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
$testSave = 'path\to\pristine-test-save-parent' # contains save_0
python tools/cpu_candidates.py --tree $tree `
  --exe "$tree\build\cpu-candidates\candidate\savagewind.exe" `
  --data-root "$tree\build\smoke-data" --save $testSave `
  --ini "$tree\build\cpu-candidates\owner-settings.ini" `
  --out "$tree\build\cpu-candidates\repeat" `
  --live --rounds 3 --duration 15 --routes plateau castle
```

The test-save parent must contain `save_0`. Use an isolated data root, a new
output directory and `pwsh`; the harness copies the save/settings for each boot.
The five default configurations are `control binding writes driver all`.
`--live` switches masks through the opt-in developer command `cpuopts`; ordinary
play defaults to mask 0. Existing descriptor hashes and resource invalidation
remain in use. No write detection or GPU synchronization is disabled.

Omit `--live` for separate boots; add `--baseline-exe` for an installed-binary
comparison. Those options cannot be combined. Keep every Windows executable
named `savagewind.exe` in its own folder with the matching game DLL; renaming
one breaks the DLL's runtime imports.

## Verification

Windows native build and gameplay completed. Synthetic ASan/UBSan checks passed
24,000 descriptor comparisons and 13,206 driver layouts per descriptor-cache
mode (enabled/disabled), plus live-switch validation. Plateau control/all
screenshots were inspected. No new full Vulkan-validation or long-session run.
The native build ignored nine stale PGO function profiles. Linux runtime compile
was unavailable because SDL3 headers were missing; the standalone checks passed.

```sh
git show cb202ef:runtime/nvn/vulkan/exec.c > /tmp/swr-reference-exec.c
python3 tools/test_vk_stage_prepare.py --reference /tmp/swr-reference-exec.c
python3 tools/test_feedback_alias.py
```
