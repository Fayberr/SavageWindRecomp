# Setup verification, 2026-10-28 and 2026-10-29

Source baseline: upstream `test` at `8368fa49da3f368c8c18dff411fd92f06f1fc25a`.
Upstream advanced by two README-only commits during setup, to `8bac5b7548862eee55e09194673298f5e9282b95`;
the implementation tested here is unchanged.

## Passed on Ubuntu 24.04 aarch64

- Complete native runtime build, including the Vulkan backend, UI and FidelityFX components.
- `test_game_sites.py`: 38 checks.
- `test_layout.py`: 59 checks, using synthetic NSOs.
- `test-bind-capture`: confirm releases, assignments, cancellation and timeout.
- `test-vibration`: HID packets, batching, motors, focus, disconnect and reboot state.
- `test_romfs`: 87 synthetic files, 3,349,500-byte image, randomized/multithreaded reads.
- Shader fuzz: 2,000 candidates, 1,339 translated GLSL shaders compiled and validated;
  zero compile or validation failures. The remaining 661 candidates were unsupported translations,
  with zero cases where the MSL path translated but GLSL did not. This is not 100% shader coverage.
- Portable SIMD sample: 1,000 encodings, 4,000 cases against Unicorn, zero failing cases/encodings.
  On ARM this needs explicit `cpu.h` then `simd.h`; the Linux link needs `-lm`.
  `extra-checks-linux.sh` supplies both without editing upstream tests.
- Launcher opened under Xvfb using Mesa llvmpipe. Screenshot visually inspected: the game-files setup
  dialog rendered, with version 0.3.1-alpha.3. Process stayed alive for the 10-second capture and exited
  with status 0 after the test sent SIGTERM. No game files were configured.
  Local screenshot: `build/contrib/launcher-smoke.png` (intentionally not committed).
- Bash syntax checks. The global vibe-check commit hook is installed.

## Passed on Windows 11, 2026-10-29

- Source synced into `X:\.Development\Projects\SavageWindRecomp`; local Git metadata initialized without
  replacing source files. Machine: Ryzen 7 5700X3D, RTX 3080 Ti, driver reported by Windows as 32.0.16.1047.
- MSYS2 UCRT64 installed from the checksum-verified official archive, then updated. Clang/lld 23.1.4,
  SDL 3.4.18, FFmpeg 9.0.2, shaderc 2026.4, Vulkan headers/loader/validation layers 1.4.363.0.
  python.org Python 3.12 virtualenv uses the same pinned generator dependencies as Linux.
- Full native build: `build/savagewind.exe` and `build/savagewind-config.exe`, no generated game library.
- Game-site/layout checks: 38 + 59. Binding capture and synthetic vibration checks passed.
- Shader sample: 2,000 candidates, 1,339 GLSL shaders compiled and validated, zero compile/validation failures;
  remaining 661 unsupported translations, none supported by the MSL path alone.
- Portable SIMD: 1,000 encodings, 4,000 cases, zero failing cases/encodings.
- Both PowerShell helpers parsed under pwsh 7.6.9. `smoke-windows.ps1` ran through a temporary Interactive
  task, using the current Windows identity SID because constructing the account from environment variables
  failed account mapping over SSH. The task was removed afterwards.
- Launcher rendered its setup screen on the real **RTX 3080 Ti**, Vulkan **1.4.341**, and wrote a 1280x720
  screenshot after 30 frames with exit code 0. Screenshot visually inspected. Validation was requested on
  a repeat run; no validation errors were logged. This tests the launcher, not gameplay or performance.
  Evidence: `build/contrib/windows-smoke/{result.json,launcher.png,stderr.log}` on Windows;
  copied screenshot at `build/contrib/windows-launcher.png` on Linux.
- Global vibe-check commit hook installed and verified through `git hook run`: ordinary message accepted,
  synthetic AI trailer rejected. The installer needed Python UTF-8 mode over SSH, and the hook uses LF.
- The revised shared `check.sh` also passed again on Linux, including the RomFS test.

## Not verified or not passing

- Gameplay, generated game-code compilation and physical controller behavior: no compatible Switch game files
  were supplied. The user's Cemu directory contains the Wii U game, update and DLC, with no Switch ExeFS/RomFS.
  Those files were inspected only at the directory level and not imported or changed.
- Windows standalone RomFS target: fails to link because its command omits the Windows `pread` implementation.
  Adding the complete platform file needs more runtime dependencies. `check.sh` reports this test as not run
  on Windows; it remains required and passing on Linux. No test body or runtime source was changed.
- Vulkan pipeline harness: upstream's script uses Windows linker names. Adapting those names and the VMA
  include path still fails to link because the current harness omits dependencies for `plat_remove_tree`,
  `vk_replace_file`, `vk_device_fault_report`, `smaa_hlsl_src` and RomFS image functions. Do not call it passed.
  The runtime itself links successfully. This may be a focused contributor tooling fix later.
- `vibe-check contrib --no-cache` still reports AI co-author trailers in inherited upstream git history
  (28 in the original scan, 25 in the current history window).
  No findings were reported in the new contributor files. Upstream history was preserved; the new commit
  has no AI co-author trailer.
- The original relay/Syncthing outage ended after the user's Pi reboot on 2026-10-29. Pi and WinPC SSH now work,
  and source propagation was verified. The outage's root cause has not been established.

Build outputs, source archives, downloaded dependencies, screenshots and runtime data stay in ignored `build/`.
No game files, keys or generated game code were obtained or committed.
