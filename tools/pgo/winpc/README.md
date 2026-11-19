# WinPC kit for the max-FPS play build (2026-11-19)

The scripts that built, trained, measured and launched the max-FPS build on Fabian's WinPC (XYFERCMD, 5700X3D + RTX 3080 Ti).
Paths are those of that machine (`X:\.Development\swr-wt\max-fps`, `C:\Users\derfa\swr-check\maxfps`); they are kept as they
ran, so a run elsewhere needs them changed. The full recipe and the numbers are in the brain note
`notes/reverse-engineering/swr-play-build-max-fps-20261119.md`.

| file | what |
| --- | --- |
| `build.sh` | the first full build: `make gen` (game code) beside the runtime, then the game library and link into `build\`. It links the runtime with an older profile (`fresh.profdata`); only `swgame.dll` and its `.id` are used from it afterwards |
| `build-gen.sh`, `build-use.sh` | the instrumented runtime (`PGO=gen`, `build-gen\`) and the profile-guided one (`PGO=use LTO=thin`, `build-pgo2\`) |
| `pgo.ps1` | stages `build`, `smoke`, `train`, `merge`, `use` as an interactive scheduled task (`SWR-PGO-<stage>`), status in `status.txt` |
| `poll.sh`, `pollab.sh` | wait on the status files over ssh from the Linux side (a call at most about 9 minutes) |
| `ab.ps1`, `ab-start.ps1` | the same-spot A/B with `tools/perf_routes.py` (old play build, max-fps old profile, new profile, mask 2), 3 rounds |
| `launch.ps1` | starts a build with the personal save: `launch.ps1 -Build build-pgo2 -Mask 3314` |
