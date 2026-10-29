# Contributor environment

Prepared on 2026-10-28 from upstream `test` commit `8368fa49da3f368c8c18dff411fd92f06f1fc25a`,
with Windows setup continued on 2026-10-29 after the Pi relay recovered.
These helpers belong to the contributor's setup branch, not to an upstream feature submission.

## Project state

[Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp) statically recompiles the Switch version of
Breath of the Wild, with its own host runtime implementing Switch services and NVN graphics on Vulkan.
It needs the user's extracted ExeFS and merged RomFS for version 1.9.0. It does not include game files.

- Latest published Windows build: **0.3.1-alpha.3**, 2026-10-25, an AMD/Intel graphics hotfix.
  Linux releases remain **0.3.0-alpha.3**, 2026-10-24. ARM gameplay is experimental.
- The Alpha 3 release notes report the opening and Great Plateau at 60 fps, and a walk through the whole map
  using a completed save. A complete quest progression or Divine Beast playthrough has not been established.
  The README's warning that nobody has played beyond the Plateau is stale relative to those release notes.
- Users report later gameplay, including Gerudo Desert, but also progression freezes, graphics problems,
  crashes and frame-rate-dependent physics. It is an alpha for testing, not a verified full-game release.
- Published Alpha 3 has keyboard/mouse and controller input, saves, sound, ultrawide, FOV, resolutions up to 4K,
  shader precompilation and configurable frame rates. Published Alpha 3 does not support DLC.
- `test` contains newer development, including DLC setup and controller vibration. Do not confuse that with
  the published release. Check its latest commits and open PRs before choosing work.
- AI-assisted contributions are explicitly accepted. Contributions must work and be tested; graphics fixes
  need an actual in-game visual check on the stated GPU/driver.

Primary references:
[Alpha 3](https://github.com/Revivedx/SavageWindRecomp/releases/tag/v0.3.0-alpha.3),
[hotfix](https://github.com/Revivedx/SavageWindRecomp/releases/tag/v0.3.1-alpha.3),
[contributor rules](https://github.com/Revivedx/SavageWindRecomp/blob/test/CONTRIBUTING.md),
[boss freeze #196](https://github.com/Revivedx/SavageWindRecomp/issues/196),
[arrow physics #144](https://github.com/Revivedx/SavageWindRecomp/issues/144),
[cutscene hang #224](https://github.com/Revivedx/SavageWindRecomp/issues/224).

## Linux

The prepared host is Ubuntu 24.04 on aarch64, with 4 KB pages, Clang 18, Python 3.12, system FFmpeg,
shaderc and the Vulkan loader. SDL 3.4.16 and Vulkan 1.4.350.1 headers are built/installed into
`build/contrib/deps`, with archives checked against upstream's pinned SHA-256 values.
Ubuntu 24.04 lacks SDL3 and its Vulkan headers predate the dynamic-rendering local-read types needed by `test`.

```bash
contrib/setup-linux.sh
contrib/check.sh
contrib/extra-checks-linux.sh
source contrib/env.sh
./build/savagewind --config
```

`contrib/check.sh` runs the game-site/layout checks, input binding capture and vibration, then builds the runtime.
It also runs the synthetic RomFS test on Linux; the standalone upstream target lacks its platform dependency
on Windows, which is reported explicitly. The extra checks sample shader translation/validation and portable SIMD
against Unicorn. All use synthetic input, with no game files required.
The extra SIMD sample is not a full opcode enumeration or an exhaustive verification.

On this VPS Vulkan uses Mesa llvmpipe, a CPU software renderer. A passing launcher or pipeline check here
does not establish hardware rendering correctness, game playability or performance on Windows.
Disk space is limited: keep full game data and generated game builds on the Windows development drive.

## Windows

Use a Windows-local checkout at `X:\.Development\Projects\SavageWindRecomp`. Source sync excludes `.git`,
Python environments and build outputs, so a synced source directory is not an initialized Windows checkout.
The Linux virtualenv cannot be reused on Windows.

For a fresh checkout:

```powershell
git clone --branch setup/contribution-environment https://github.com/Fayberr/SavageWindRecomp.git X:\.Development\Projects\SavageWindRecomp
Set-Location X:\.Development\Projects\SavageWindRecomp
pwsh -NoProfile -File contrib/setup-windows.ps1
```

If source files have already arrived through Syncthing, run the last two lines only. The wrapper initializes
missing Git metadata with a mixed reset, preserving source files, then calls upstream's setup using `pwsh`.
It installs MSYS2 UCRT64 dependencies and a separate python.org virtualenv, pins generator dependencies,
runs source checks and builds the runtime. If MSYS2 is absent, it uses the official 2026-09-27 self-extracting
archive and verifies its published SHA-256. Python uses UTF-8, and WindowsApps is added to the process PATH
for winget. It does not launch the game or change GPU drivers.

On a first installation, a mirror returned HTTP 429. The local MSYS2 configuration now prioritizes the
official `https://repo.msys2.org` repository and uses `ParallelDownloads = 1`; original configs are backed up
next to them with `.contrib-original` suffixes. Package signature verification stays enabled.

The Pi reboot restored the relay on 2026-10-29. WinPC access and source propagation are verified. The target
is a Ryzen 7 5700X3D with RTX 3080 Ti. Check [the verification record](VERIFICATION.md) for build/test results.

For extra synthetic checks, use MSYS2 UCRT64:

```bash
bash contrib/extra-checks-windows.sh
```

For an automated launcher screenshot on the logged-in desktop, from local or SSH `pwsh`:

```powershell
pwsh -NoProfile -File contrib/smoke-windows.ps1
```

It creates and removes a temporary Interactive task, uses isolated data/settings/save paths under
`build/contrib/windows-smoke`, and asks the launcher to save its own screenshot after 30 frames and exit.
No game files are required. A bounded wait stops a stalled test process. The screenshot covers only the app.
Validation is requested, and stdout/stderr are captured alongside the result. The test uses the current Windows
identity SID for its task principal, so it also works when OpenSSH's account-name environment is incomplete.

## Gameplay and automated testing

Gameplay needs the user's own extracted Switch BotW **1.9.0** files. No suitable files were supplied for this setup.
Once available, inspect the build ID with `tools/venv/bin/python tools/sw_probe.py` (Windows: `Scripts/python.exe`),
then follow upstream's `make gen` and `make -j` instructions. The plain runtime build does not include the
standalone generator/compiler bundle that a player release uses for first-time game setup.

Use a separate data directory, test save copy and configuration. Upstream provides:

- `SAVAGEWIND_DATA_ROOT`, `SAVAGEWIND_SAVE_DIR`, `SAVAGEWIND_SETTINGS` for isolation.
- `SAVAGEWIND_INPUT` for scripted gamepad actions; `SAVAGEWIND_SCRIPT_CLOCK=1` uses console-frame timing.
- `SAVAGEWIND_SHOT_AT`, `SAVAGEWIND_WINDOW_SHOT_AT`, `SAVAGEWIND_EXIT_AFTER_SHOT` and
  `SAVAGEWIND_EXIT_AT` for screenshots and bounded runs. Inspect definitions before scripting a scenario.
- `tools/linux/test_run.sh` for bounded runs using a copied save, with logs/screenshots under `build/`.
- `tools/test_translate.sh` and `tools/test_vk_pipelines.sh` under MSYS2 for shader/pipeline regression checks.
- `SAVAGEWIND_VK_VALIDATION=1` with MSYS2's validation layer path for Windows diagnostics.

SSH commands run outside the interactive Windows desktop. For a visible GPU/game test, use the existing
interactive desktop/task mechanism. Avoid MSYS `timeout` for game windows, as upstream documents a stall.
Start comparative gameplay tests at 30 fps, then test 60 separately because physics regressions are reported.
The Vulkan pipeline harness currently fails to link against this source baseline after translating its
Windows library names to Linux names; see [the verification record](VERIFICATION.md).

## Git workflow

The remotes are `upstream` (Revivedx) and `origin` (Fayberr's fork). Development and PRs target **upstream `test`**;
`main` tracks releases. Keep this setup commit out of unrelated upstream fixes: create each fix branch from
the latest `upstream/test`, and copy only the local helpers you need without staging them.
Use focused commits and describe exactly which checks and in-game scenarios passed.

Never commit game files, generated game code, extracted shaders/assets, console fonts, saves or keys.
The upstream ignore rules cover those inputs and runtime outputs; all helper build output stays in `build/`.
Do not copy implementation code from game decompilation projects. Read `CONTRIBUTING.md` before a submission.
