# CPU stage-preparation experiments

The renderer prepares the resource inputs of both shader stages on every draw.
The existing descriptor cache avoids most Vulkan allocations and updates, but
does not avoid constructing their input writes. These independent experiments
reduce preparation without changing the resources resolved or cache validity.
They are off by default while their performance is evaluated.

`SAVAGEWIND_VK_CPU_OPTS` is a bit mask, read once per process:

| Bit | Change |
| --- | --- |
| 1 | Build an ordered binding plan when a graphics or compute program is compiled. Preserve reflection order and the late scale/tint writes, while avoiding two scans of both stages for each draw. |
| 2 | Resolve resource data as before, but construct `VkWriteDescriptorSet` objects only after a descriptor-cache miss. Keep both existing content hashes, submission lifetime and invalidation. |
| 4 | Clear only the driver-constant bytes that can be copied, and omit graphics storage slots outside that range. Preserve compute's minimum 0x500-byte span. |

`0` is the candidate binary's control path; `7` enables all three. The controls
do not change guest-memory snapshots, texture freshness, dynamic offsets, GPU
barriers or deferred destruction. Program plans are published with the existing
atomic program-ready state and have the same lifetime as the program.

## Correctness checks

Freeze the previous executor source before editing, then run:

```sh
git show cb202ef:runtime/nvn/vulkan/exec.c > /tmp/swr-reference-exec.c
python3 tools/test_vk_stage_prepare.py --reference /tmp/swr-reference-exec.c
python3 tools/test_feedback_alias.py
```

The differential test compiles the actual preparation functions with synthetic
resource providers under AddressSanitizer and UndefinedBehaviorSanitizer. It
compares write contents and order, resource-provider calls, dynamic offsets,
cache hits and generation invalidation for all eight masks. It includes empty
layouts and runs with the descriptor cache enabled and disabled. Driver packing
is compared for every byte size from 0 through 2200 and all six stages.

These checks do not replace a native build, gameplay inspection or Vulkan
validation. Windows sparse guest-memory handling is unchanged by the experiment.

## Interleaved measurements

`tools/cpu_candidates.py` wraps the existing `tools/perf_routes.py` harness. Pass
the existing checkout, an isolated data root, a pristine test-save folder, a
settings file and an output directory using its required options. It compares
the control, each bit separately and all bits together, rotating their order on
subsequent rounds. It uses 1440p output, uncapped rendering and no frame
generation. The input settings provide upscaling, anti-aliasing, shadows and
draw distance. Stack sampling and per-draw timers are disabled; whole-list
timing and frame tracing remain enabled consistently for every configuration.

Use `--baseline-exe` to include the previously installed executable. On Windows,
place that binary in a separate folder as `savagewind.exe` alongside its matching
`swgame.dll` and ID. Do not rename the executable: the game DLL imports runtime
functions by that module name. This baseline distinguishes runtime-switch gains
from compilation or profile-counter effects.

Each boot receives a fresh copy of the test save and a separate settings file.
The manifest records executable and settings hashes. Results, logs and
screenshots remain private build artifacts. Compare FPS and frame time per
route, and report slow frames separately. Individual gains need not add to the
combined gain. A screening round is not enough to establish small improvements;
repeat promising configurations in an interleaved order before selecting a
play build.
