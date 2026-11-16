# Alpha 6 renderer optimization hunt

This experiment starts at the Alpha 6 play tree, `6eda961`, with PGO/ThinLTO and
PR824/858. It keeps all new renderer behavior opt-in. It does not replace the
installed executable or the play shortcut.

## Prior work and scope

Fragment preparation reuse was already prototyped in November 13's CPU hunt.
It had a local CPU saving, but its final across-boot comparison did not establish
an FPS improvement. This experiment revisits that known approach, and compares
it with a refinement: cache by the fragment shader and layout across vertex
programs, and compare only the uniform ranges that stage actually uses.

PR862's binding partition, descriptor-cache associativity and index-scan bundle
was closed after its author found no measurable native improvement. It is not
reapplied here. PGO and refreshing stale profiles are also known levers, not new
discoveries from this hunt.

## Fragment-material prototype

`SAVAGEWIND_VK_STAGE_REUSE=0` keeps the original preparation path, 1 uses the
conservative earlier strategy, and 2 enables material-scoped reuse. When
`SAVAGEWIND_VK_CPU_BENCH=1` and the control endpoint are enabled, `cpuopts 0|1|2`
changes modes in one loaded scene and returns reuse counters.

The original `stage_set` preparation function is byte-for-byte unchanged from
the play source. The wrapper skips it only for ordinary fragment stages.
It still runs all earlier texture resolution, upload, feedback, viewport and
shadow decisions. Storage/global-memory stages and constant-buffer patch paths
fall back. Keys include submission and descriptor-pool lifetime, guest-copy,
uniform-update and GPU-write generations, used buffer ranges, compiler constants,
resolved images, scales, viewport scale and tint. Cached dynamic offsets retain
the order required by Vulkan.

## Native comparison

Windows, Ryzen 7 5700X3D, RTX 3080 Ti. Same process per scene, 2560x1440, copied
owner graphics settings, uncapped and VSync/FG off. Three rotated forward/reverse
rounds, twelve seconds per mode per round. No capture, sampler or per-draw timers;
whole-list profiling and the frame trace remain enabled. The test uses fresh
copies of a test save, its own data root, and muted copied settings.

These are medians of the three windows for each mode, not a guarantee for play:

| Scene | Original FPS | Conservative FPS | Material FPS | Original render CPU | Material render CPU | Material p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Plateau | 100.20 | 103.17 | 106.97 | 9.386 ms | 8.783 ms | 13.559 ms |
| Kakariko | 110.41 | 116.39 | 118.68 | 7.787 ms | 7.333 ms | 12.544 ms |

Ratio-of-median FPS changes are +6.75% and +7.49%. Paired median render-CPU
savings are smaller than a subtraction of independent medians: 0.395 ms at
Plateau and 0.410 ms at Kakariko. Material reuse has lower render CPU in all six
paired comparisons. Its refinement over conservative reuse saves a paired median
of about 0.17 ms in each scene, so do not call the refinement a large new win.

The windows vary. The first Kakariko control window has a 29 ms p99; cloud cover
also changes despite fixed hour and the weather override. Screenshots were
inspected: no obvious new geometry, material or HUD corruption; lighting and
wind differ, so no pixel-equality claim. No long-session or full Vulkan-validation
claim is made. The candidate remains experimental and has not been promoted.

## Verification and reproduction

`tools/test_vk_stage_reuse.py` extracts the actual C functions and runs them
under ASan/UBSan. It checks cross-program reuse, unused-binding changes,
independent write/lifetime invalidation, compiler-constant changes, special-stage
fallbacks, dynamic offsets and 2,000 descriptor comparisons, with the normal set
cache both enabled and disabled. Feedback-alias checks also pass.

The Windows PGO/ThinLTO runtime build, matching game-library ID and loader health
check pass. The tests and benchmark helpers pass vibe-check. The benchmark uses
PowerShell 7, mutes copied settings before launch, and distinguishes owned game
processes by PID, including when the test executable is outside the source tree.
Process-query failure stops the benchmark. The ownership regression checks cover
both an external owned executable and an unrelated game inside the same tree.

```sh
python3 tools/test_vk_stage_reuse.py
python3 tools/test_perf_routes_ownership.py
python3 tools/test_feedback_alias.py
python3 tools/gen_launch_flags.py --check
python3 tools/stage_reuse_bench.py --help
```

Raw results, frame traces, screenshots, exact binary hashes and native logs stay
in ignored build artifacts. The native renderer binary was built from `2e38300`;
later commits add harness checks and documentation, not renderer changes.

## Exact-source PGO check

A separate instrumented build of the unchanged `6eda961` play source trains a
fresh profile. The resulting use build is compared with the installed binary on
Plateau and Kakariko, which are outside the training scenes. This comparison
contains no material-cache changes and uses different binaries, so it requires
relaunches and cannot have the same-process control of the first experiment.

Results are pending while this report is being completed.
