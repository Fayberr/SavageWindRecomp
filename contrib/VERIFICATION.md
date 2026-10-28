# Setup verification, 2026-10-28

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

## Not verified or not passing

- Gameplay, generated game-code compilation, physical controller behavior and Windows GPU behavior:
  no user game files were supplied, and remote Windows access was unavailable.
- Windows setup wrapper: prepared and reviewed, but not executed or parsed by a Windows `pwsh` runtime.
- Vulkan pipeline harness: upstream's script uses Windows linker names. Adapting those names and the VMA
  include path still fails to link because the current harness omits dependencies for `plat_remove_tree`,
  `vk_replace_file`, `vk_device_fault_report`, `smaa_hlsl_src` and RomFS image functions. Do not call it passed.
  The runtime itself links successfully. This may be a focused contributor tooling fix later.
- `vibe-check contrib --no-cache` reports 28 AI co-author trailers in inherited upstream git history.
  No findings were reported in the new contributor files. Upstream history was preserved; the new commit
  has no AI co-author trailer.
- Syncthing: Pi peer disconnected. SSH: local reverse relay port 2222 refused connections.
  Neither Windows source propagation nor Windows package installation is established.

Build outputs, source archives, downloaded dependencies, screenshots and runtime data stay in ignored `build/`.
No game files, keys or generated game code were obtained or committed.
