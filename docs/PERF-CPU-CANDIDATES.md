# CPU candidates handoff

The latest continuation is documented below. All runtime candidates remain
opt-in on branch `perf/cpu-candidates-20261113`. The earlier experiment and its
original-executable restoration are retained under Earlier results for context.


## Steady-100 continuation, 2026-11-13

The fixed routes are stationary views. Castle holds Link in fly mode above
Castle Town; the glider animation repeats. They measure repeatable renderer
throughput, not traversal performance. Benchmark volume is now muted in memory;
the owner's saved audio and visual settings are not changed.

### Baseline before edits

Exact restored play binary, PGO/ThinLTO plus PR762/763, 1440p output,
FSR1 Ultra Quality and FXAA. One 20-second sample per route, no stack sampler
or per-draw timers. The existing whole-list timers and frame trace remain on.

| Route | Average FPS | Renderer CPU ms/frame | Seconds >=100 FPS | Lowest second |
| --- | ---: | ---: | ---: | ---: |
| Plateau | 100.55 | 9.20 | 11/20 (55%) | 55.48 FPS |
| Castle | 101.46 | 9.02 | 14/20 (70%) | 69.76 FPS |

Neither route passes the requested 90% of seconds >=100 FPS and no second
below 90 FPS. `tools/fps_windows.py` computes complete, nonoverlapping one-second
windows from the frame trace, discarding the first partially traced frame and
the final partial second. A frame crossing a boundary contributes the fraction
of its duration inside each window. The four synthetic tests cover exact 100,
partial tails, long crossing frames and a high average hiding a slow second.

### Profile and pre-build estimates

The eight-second Castle capture contains 312 frames, 3,811 draws/frame and 7,622
stage preparations/frame. Profiler-on throughput is 39.04 FPS, with 5.21 ms GPU
work/frame. Its wall scopes are inflated by profiling and host scheduling:
sets 4.33 ms, draw preparation 2.50, textures 2.33, vertices 1.37. Do not use these
as play-build costs or add nested sampler rows.

Active renderer sample shares normalized to the baseline's 9.02 ms give rough
budgets of 2.11 ms for stage_set, 0.91 for texture_info (including 0.64 in its
miss path), 0.41 for begin_pass, and 0.29 each for guest_fetch and vk_alloc_set.
The profile's formatting and recorder overhead is not a play-build saving.

Only unchanged-fragment preparation cleared the 0.5 ms build threshold:
roughly 1.05 ms fragment preparation x 60-75% reuse minus 0.15 ms checks,
about 0.5-0.65 ms. Extra guest-copy reuse was estimated below 0.3 ms because the
existing command-list cache already hits roughly 90%; another texture memo
at 0.2-0.4 ms, and formatting cleanup below 0.1 ms because its expensive path
is gated off outside captures. No builds were spent on those ideas.

### Candidate and short comparison

New bit 8 skips fragment-stage preparation before cbuf_info, constant-table
copies and set hashing. The previous descriptor and exact dynamic offsets are
reused only when program, bound ranges, resolved texture descriptors/scales,
tint, viewport scale, guest/UUB/GPU-write generations, submission and descriptor
pool generation match. Storage/global-memory shaders and active constant-buffer
patches retain the full path. Vertex preparation and the earlier texture,
feedback, upload and viewport work always run. Runtime default remains 0.

One loaded Castle game, three 10-second samples each, with the existing rotated
forward/reverse order. No relaunch between masks; `--quick` avoids repeated
camera positioning and GPU profiling between samples.

| Mask | Path | Renderer CPU median ms | Renderer range ms | FPS median |
| --- | --- | ---: | --- | ---: |
| 0 | Control | 10.513 | 10.004-11.015 | 83.52 |
| 2 | Writes | 10.164 | 9.886-10.299 | 90.61 |
| 8 | Preparation reuse | 9.404 | 9.191-9.914 | 96.58 |
| 10 | Writes + reuse | 9.816 | 9.331-9.997 | 90.77 |

Control minus reuse within each round is 0.599, 0.601 and 1.824 ms. The paired
median saving is 0.601 ms; the independently medianed CPU difference is 1.109 ms.
The latter is not a guaranteed gain. About 49.2% of stage calls qualify and 62.3%
of those reuse preparation (30.7% of all stage calls). This meets the estimated
opportunity. The extra benefit of writes in combination is ambiguous; mask 8 is
the selected path for the final check. No additional timing rounds are planned.

Vesktop, Opera, Syncthing and system activity consumed substantial background
CPU during the session; they were observed, not stopped. The first profiling
attempt failed its hop, and the first quick attempt had no camera; neither took
usable comparison samples. The harness now waits for camera readiness. Audio
was muted live during the short comparison after the owner heard the repeated
glider; subsequent launches mute the test setting automatically.

### Final check and play decision

One final 20-second measurement on each route, mask 8 only:

| Route | Average FPS | Renderer CPU ms/frame | Seconds >=100 FPS | Lowest second | Seconds below 90 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Plateau | 74.62 | 11.546 | 0/20 (0%) | 48.38 FPS | 19/20 |
| Castle | 84.03 | 9.959 | 4/20 (20%) | 50.94 FPS | 11/20 |

The target is not met. The different-time route comparison is worse than the
initial baseline; it does not verify a two-route improvement. Main-thread CPU
also rose from 6.40 to 8.02 ms at Plateau and 7.25 to 8.29 at Castle, which this
fragment-only change should not directly cause. Background load and scheduling
are material confounders, not an established complete diagnosis. Screenshots
also show Castle weather differs between boots despite the route's weather
preset (the short live session stayed rainy). No extra timing runs were added.

Both route screenshots were inspected against baseline: scene geometry,
materials, shadows and HUD have no obvious new corruption. Animation, weather
and slight post-hop height differences prevent a pixel-equality claim. Final
Plateau matches the baseline position; final Castle is 0.32 m lower than the
baseline after the same hop and fly operation.

**Do not promote this candidate for play.** The local same-process preparation
saving is promising, but steady 100 and a two-route benefit remain unverified.
Aktueller Build retains the original PGO/ThinLTO play executable, matching the
pre-session hash. The candidate remains archived separately with its matching
game DLL/id; no saved settings or personal save changes are required. All test
games are closed. No new upstream PR or public comment is part of this pass.

### Structural next steps and estimates

Further small caches do not supply the requested budget. Even the promising
skip experiment's paired median is about 0.60 ms, versus the initial request's
roughly 2.5 ms Castle deficit. The profile puts only about 0.27 ms in texture memo
lookup overhead (0.91 inclusive minus 0.64 miss resolution), and normal-play
formatting is gated off. Guest-copy optimization overlaps the descriptor pool;
its budget cannot be added again. Removing all remaining preparation would be
a structural change, not another result cache. Host contention still needs to
be separated from renderer work before any steady-FPS guarantee.

These are planning estimates, not measured gains, and overlap:

| Next experiment | Napkin saving, render CPU/frame | Design and stop gate |
| --- | --- | --- |
| Push descriptors for ordinary stages | 0.5-0.8 ms | Roughly 0.3 ms allocation plus 0.3-0.6 ms lookup/hash work, minus 0.1 ms extra command work. Check supported limits and stage binding counts first; keep special stages on the current path. Reject if the same-process median saves under 0.5 ms. |
| Persistent indexed material descriptors | 1.0-1.7 ms | Remove 60-75% of roughly 1.5-2.4 ms repeated descriptor/texture work, less 0.15-0.25 ms table/index work. Requires shader/binding changes and lifetime-safe image/sampler retirement, while retaining upload and feedback checks. |
| Two-worker immutable draw preparation | 1.0-1.5 ms | Parallelize 2.5-3.5 ms of preparation: half saved minus 0.25-0.3 ms snapshots/queue/order overhead. Capture guest constants and mutable state in submission order before workers; commit Vulkan work in original order. The existing recorder thread is already separate and is not counted as a new saving. |

Start with a small push-descriptor feasibility spike. If it cannot clear the
0.5 ms gate, choose indexed materials or immutable draw preparation instead of
building more tiny caches. Keep switches, differential descriptor/offset and
write-invalidation tests, both-route screenshots and the same short live A/B.
Before a future final run, hold or record weather and record background CPU and
CPU frequency consistently; do not silently stop the owner's applications.
No structural renderer implementation or further PC launch was started here.

### Verification and reproduction

Native Windows PGO/ThinLTO build passes. Nine stale PGO function profiles are
ignored, as in the earlier experimental build. Synthetic ASan/UBSan comparisons
pass 48,000 descriptor cases and 13,206 driver layouts per set-cache mode, all 16
masks, dynamic-offset reuse and invalidation after 16 independent input changes.
Special-stage fallbacks and feedback-alias regressions pass. No new full Vulkan
validation or long-session claim is made.

```sh
git show cb202ef:runtime/nvn/vulkan/exec.c > /tmp/swr-reference-exec.c
python3 tools/test_vk_stage_prepare.py --reference /tmp/swr-reference-exec.c
python3 tools/test_fps_windows.py
python3 tools/test_feedback_alias.py
python3 tools/fps_windows.py <route>_frames.csv
```

Use `cpu_candidates.py --live --quick --rounds 3 --duration 10 --routes castle
--configs control writes reuse writes_reuse` with the normal tree, executable,
data-root, pristine save parent, settings and fresh output arguments.
For the final check use `--live --rounds 1 --duration 20 --routes plateau castle
--configs reuse`, without `--quick`.

Private evidence stays in ignored `build/cpu-100fps/`: baseline and final
traces, short live results, source/binary hashes, profile and screenshots.
Generated artifacts and saves are not committed.

## Earlier results, 2026-11-13

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
  --exe "$tree\build\cpu-100fps\candidate\savagewind.exe" `
  --data-root "$tree\build\smoke-data" --save $testSave `
  --ini "$tree\build\cpu-candidates\owner-settings.ini" `
  --out "$tree\build\cpu-100fps\repeat" `
  --live --quick --rounds 3 --duration 10 --routes castle
```

The test-save parent must contain `save_0`. Use an isolated data root, a new
output directory and `pwsh`; the harness copies the save/settings for each boot.
The four default configurations are `control writes reuse writes_reuse`.
Legacy masks `binding`, `driver`, `all` (7) and `binding_writes` remain explicit
options for reproducing the earlier experiment.
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
