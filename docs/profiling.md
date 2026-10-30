# Profiling Savage Wind

SWR has one profiler. It is off until you ask for a capture, costs about one branch per marker while off, and writes
one file per capture. A tool (`tools/swprof.py`) turns that file into text, JSON or CSV. Nothing is sent anywhere and
no viewer is needed.

## Taking a capture

| How | What it does |
| --- | --- |
| `F6` in the game | Starts a capture; `F6` again stops it. |
| `SAVAGEWIND_PROF=<first>[:<frames>]` | Starts at frame `<first>`; stops after `<frames>` frames (without a count: stays on until stopped). `SAVAGEWIND_GPUPROF_AT=<n>` still works and means `<n>:1`. |
| `prof start`, `prof stop`, `prof status` | The same, from the command layer (once it is merged). |

A capture also ends by itself when its buffer is full (`SAVAGEWIND_PROF_MB`, default 256, 8 to 4096). The file is
written in the background to `profile/swprof-<date>-<time>.swprof`. A capture started while the last file is still
being written is refused; wait for `prof status` to say `idle`.

While a capture runs the game is slightly slower. Keep captures short (5 to 30 seconds) and take them at the moment
the problem happens.

## Reading it

```
python3 tools/swprof.py                      # the newest capture in ./profile
python3 tools/swprof.py FILE --exe PATH      # name the runtime's functions (PATH = the binary that was running)
python3 tools/swprof.py FILE --frames 120:240 --top 40 --slow 25
python3 tools/swprof.py FILE --json out.json --csv frames.csv
```

The report has, in this order:

- **Frames**: rate, average, p50, p95, p99, max, how many were over 33 and 50 ms.
- **Top places by self time**: the zones and added-up work with the most time of their own, per frame.
- **GPU**: busy and idle time, then every pass, dispatch and copy by GPU time. GPU rows are aligned to the CPU clock,
  so a slow frame can be matched to what the GPU was doing.
- **Sampler**: where each thread was, by stack sample: game code, the SWR runtime, the driver and system, or waiting
  (inside one of the wait zones). Then the runtime's hot functions by self time, by inclusive time, and the runtime
  functions that were calling into the driver or system. Without `--exe` the functions show as `runtime+0x...`.
- **Threads**: zone tree per thread, then the added-up work (`SW_ACC`) tree.
- **Counters** and the **slowest frames** with the places that took the most time in each (and the sample split).

All times are milliseconds per frame unless a heading says otherwise. The sampler takes one stack per thread per
millisecond (`SAVAGEWIND_PROF_SAMPLE=<microseconds>`, 100 or more, `0` = off).

The recompiled game code is deliberately one total. The tool never looks up or prints a game function, and the
sampler section ends with a reminder: do not put game addresses, game names or generated code into pull requests or
issues. Runtime function names are fine.

## Adding markers

Include `prof.h`. Pick the cheapest marker that answers the question:

| Marker | Use it for | Cost while capturing |
| --- | --- | --- |
| `SW_ZONE("name")` | A stretch of work that happens up to a few hundred times a frame. One event each. | one event |
| `SW_ACC("a/b")` | Work done thousands of times a frame (a draw's steps). Summed per thread and frame; `/` nests. | two clock reads, no event |
| `SW_TALLY("name")` | Only how often something happens (cache misses). | a counter bump |
| `SW_COUNT("name", n)` | A number now: draws, bytes, queue depth. | one event |
| `prof_site_dyn(name)` | A name only known at run time (a pass's size). Cached, copied. | once per name |

Rules: no locks, no allocation, no formatting inside the game's paths; names are string literals; put the finest zones
behind `-DSW_PROF_DISABLE` if they sit in the hottest loops. Wait zones that should count as waiting in the sampler are
listed in `WAIT_ZONES` in `tools/swprof.py`.

## Platforms

- **Windows**: the sampler suspends each guest thread, reads its context and unwinds with the x64 unwind data.
  GPU time comes from Vulkan timestamps, aligned with `VK_EXT_calibrated_timestamps` when the driver has it.
- **Linux**: the sampler signals each thread (`SIGPROF`) and unwinds with glibc `backtrace()` (the `.eh_frame`
  unwind tables), so it works on x86-64 and aarch64 builds with the default flags.
- **macOS**: zones, added-up work and counters work; GPU timing (Metal) and the sampler are not there yet.

## Tests

`make test-prof` builds `tools/test_prof.c` (the recorder and the sampler, several threads, a full buffer) and runs
`tools/test_swprof.py` (the report on captures built by hand). It also prints the marker cost with the profiler off
and on.

## File format

`SWPROF01` version 1; `runtime/prof.c` has the layout in its header comment. The tool refuses files with another
magic or version.
