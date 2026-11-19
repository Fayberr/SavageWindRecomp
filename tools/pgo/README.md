# Runtime profile for `PGO=use`

`runtime.profdata` is the profile that `make PGO=use` reads (change it with `PGO_PROFILE=<file>`). It only covers the
runtime (`runtime/`), never the generated game library, and holds execution counters keyed by function names
(`file.c:function`): no game data, no paths.

The profile in the tree now is the extensive one (2026-11-19, below). The paragraphs after it describe the first profile
and its gains, kept for the numbers.

Made with clang 23.1.4 (MSYS2 UCRT64), from `test` at 04aa642. One training run of the instrumented build
(`make PGO=gen`): `tools/perf_routes.py run` over the kakariko, field, hateno, korok and rito routes at 1920x1080 with
TAA and high shadows (about 9 minutes), so the profile is of the TAA path the game is played in. The file is about
680 KB; its size follows the number of counters in the code, not the play time.

Measured on a 5700X3D + 3080 Ti (3 rotated rounds, TAA, high shadows, routes NOT in the training), against the plain
build: with `make dist`'s `PGO=use LTO=thin` plateau 1080p +33%, castle 1080p +23%, plateau 1440p +27%, castle 1440p +19%;
PGO alone +28 / +18 / +21 / +17%. The previous profile (AA off, older code) gave +1..+10% on the same routes and a merge of
the two was worse than the new one alone: regenerate instead of merging.

## The extensive profile (2026-11-19, branch `perf/max-fps` at 9d70702)

Trained on the combined `perf/max-fps` tree with `tools/pgo/train.py` (below): 11 boots, about 65 minutes, 881 KB, 4948
functions. Boots: the player's own settings at 2560x1440 (SMAA, FSR 1, ultra shadows, CPU option mask 3314) at all 7
`perf_routes.py` places, and TAA at 1080p with high shadows at plateau, kakariko, field and hateno. Each boot plays twelve
scenes (six in the TAA boots): day still, a camera pan, a fly-around that streams in new ground, the pause menu and the
map, dusk, night (still and flying), heavy rain, a thunderstorm, snow, dawn, and the day again.

Why a retrain was needed: the earlier profile came from the Alpha 6 source. The CPU option bits and the upstream merge
had edited the hot render functions, and a profile only counts for a function whose control flow is unchanged, so those
were built as if there was no profile. `make PGO=use` on the old profile warned about about 35 mismatched functions on this
tree; with the extensive one it warns about 3 (of 182 functions in two files).

Measured (`perf_routes.py run`, hidden window, 2560x1440, the player's settings, frame generation and vsync off, profiler
and frame trace off, 3 boots per build, order rotated per round, median fps_avg of 20 s at four fixed places; RTX 3080 Ti,
5700X3D). The training covers these same places, so this is the gain at places the profile has seen:

| build | plateau | kakariko | field | hateno |
| --- | --- | --- | --- | --- |
| old play build (Alpha 6 + material cache, old profile) | 122.0 | 124.4 | 148.2 | 126.4 |
| max-fps, old profile, mask 3314 | 105.1 | 120.2 | 148.8 | 118.3 |
| max-fps, extensive profile, mask 3314 | 127.5 | 128.7 | 149.1 | 132.1 |
| max-fps, extensive profile, mask 2 (material cache only) | 115.3 | 126.3 | 151.7 | 124.6 |

Field is GPU-bound (about 4.4 ms of GPU work per frame) and does not change. Not measured: places the training did not
visit, other settings, long play sessions.

## Limits

- A profile only fits the compiler that wrote it. With another clang, `PGO=use` can fail with an "unsupported
  instrumentation profile format version" error: make a new one (below).
- It goes stale as the runtime changes. A function that was edited since loses its data and is built as usual; everything else keeps working. Regenerating every so often keeps the gain. After a change to a hot function (anything in `runtime/nvn/vulkan/` that runs per draw), check the `PGO=use` build log for "profile data may be out of date" and retrain if it names more than a few functions.
- The instrumented build must have the same function structure as the normal one: the profile is keyed by a hash of the
  control flow, so `#ifdef SW_PGO_GENERATE` must not add or remove branches or loops in a function that is also profiled
  (`present_locked` calls `pgo_frame()` unconditionally for this reason).

## Making a new profile

```
make PGO=gen B=build-gen RELEASE=1 build-gen/savagewind.exe       # (copy swgame.dll, swgame.dll.id and nvngx_dlss.dll next to the exe)
LLVM_PROFILE_FILE=<dir>/rt-%p.profraw python tools/pgo/train.py --exe build-gen/savagewind.exe --tree . \
    --data-root <own set-up data root> --save <folder with save_0> --ini <savagewind.ini> --out <runs dir> --profiles <dir> \
    --env SAVAGEWIND_VK_CPU_OPTS=<the mask the game is played with>
llvm-profdata merge -o tools/pgo/runtime.profdata <dir>/*.profraw   # skip the near-empty files of a re-exec parent
make PGO=use B=build-pgo RELEASE=1 build-pgo/savagewind.exe                # read the log for "out of date" warnings
```

`train.py` boots the instrumented runtime once per place and config, plays the scenes over the control socket, and ends
each boot with `exit`, which makes the runtime write its profile (`runtime/control.c` `pgo_dump`); `--quick` is a smoke
test (a few seconds per scene, `--routes plateau --configs play` for one boot). The runtime appends a snapshot of the
counters every 3000 presented frames and at the end, and resets them after each write, so the snapshots add up to the
run. On Windows a boot can re-exec itself when the guest address space is taken: the parent leaves a near-empty profraw
and `perf_routes.wait_world` follows the child; `train.py` retries a boot once. Use a data root of its own (the first boot
compiles shaders for about two minutes, so give `--boot` 420) and a scratch copy of a save, never the player's. Do not
merge an old profile into a new one.
