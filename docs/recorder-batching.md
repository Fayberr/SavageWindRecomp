# Experimental Vulkan recorder batching

`SAVAGEWIND_VK_RECORDER_BATCH_KB=64` lets the Vulkan translation thread queue up to 64 KiB of command packets before publishing work to the recorder thread. The default stays 4 KiB. Values from 4 to 256 are accepted; invalid values retain the default. The setting is read once at startup and only matters with the recorder thread enabled.

Larger batches can reduce publication and Windows wake calls. They can also delay the recorder receiving work, so their effect depends on the scene and the CPU/GPU balance. Submission, drain and ring-space waits still publish immediately. The command order, ring capacity and wake protocol are unchanged.

On a Ryzen 7 5700X3D and RTX 3080 Ti, 2560x1440 with TAA, high shadows, device-local geometry heap copy enabled, frame generation off, no FPS cap and VSync off, interleaved A/B/B/A runs gave:

| Scene | Default 4 KiB | 64 KiB | Change |
|---|---:|---:|---:|
| Great Plateau, standing | 78.5 FPS | 86.5 FPS | +10.1% |
| Great Plateau, turning | 68.2 FPS | 75.1 FPS | +10.1% |
| Kakariko, standing | 68.8 FPS | 66.0 FPS | -4.1% |
| Kakariko, turning | 67.6 FPS | 71.6 FPS | +5.9% |

These are scene-specific observations, not a general speedup promise. Each side has two runs of 25 seconds standing or 30 seconds turning. FPS is total measured frames divided by their elapsed time. A private test harness switched only the publication threshold in the same process, with capture and sampling disabled. The save position, standing camera, noon and graphics settings were held fixed; turning used continuous right-stick input. Background desktop applications remained active, and Kakariko had larger variation between runs. The separate clean runtime build is a compatibility smoke check, not another paired FPS measurement.

The Plateau comparisons saved about 1.2-1.3 ms per presented frame, with better p95 intervals, and reduced recorder wake calls from roughly 300 to 26 per frame. Kakariko standing instead had worse p95 intervals. This is why the setting is opt-in and the existing default is retained. Other GPUs, input latency, long play sessions and transitions are not characterized by these short tests.
