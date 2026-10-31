# Profiler correctness review notes

This records the 2026-10-31 follow-up to the first working profiler. It is intended to make an independent review
possible without the development conversation. The core is independent of the control endpoint; the command glue
stays on a separate integration branch. No game files, generated code or captures belong in the submission.

## Changes and reasons

### Capture ownership and producer completion

Previously cleanup retained only a start timestamp. An accumulator opened in capture A could close during capture B
and charge its entire duration to B. The writer also slept 50 ms before reading and clearing shared storage.

`ProfZone` now retains the capture generation. Cleanup rejects another generation, and recording checks admission
again before modifying its cursor. This applies to the renderer's manual draw spans as well as SW_ZONE/SW_ACC.

The recorder has 256 bounded thread slots. A TLS key identifies a thread's stable slot; its destructor releases
ownership but retains that capture's final totals. Slots are not recycled within a capture. First registration
takes the registry mutex; steady-state records only touch the calling thread's cursor and its write-in-progress flag.

The ordering to review in `prof.c:enter` and `writer_main` is:

1. A producer publishes `busy = 1` with sequential consistency, then rechecks its generation and admission with
   sequential consistency. It only touches capture data after a successful check.
2. Stop closes admission with a sequentially consistent store. The writer closes GPU admission after draining GPU
   results, then checks every busy flag with sequential consistency under the registry mutex.
3. A producer that passed its recheck before closing must finish and publish `busy = 0` before the writer consumes
   its slot. A producer arriving after the writer's scan fails admission and never touches capture data.

The flag covers the short recording operation, not the scope being measured. Old scopes therefore cannot hold a
writer indefinitely. Final accumulator records are streamed as extra chunks, so they survive a full event pool and
worker exit. The writer compacts thread IDs for the file; dead slots from previous captures are omitted.

Normal shutdown waits for the writer. Reboot calls shutdown before guest thread handles or the game library can be
replaced. A full buffer requests stop after leaving the recording critical section and logs the reason. Writer
open/write/close errors do not publish a new successful path; a partial file is removed after write/close failure.

### Linux sampling

The old sampler restored the SIGPROF disposition while a blocked target could still have a pending signal. A harness
reproduced process termination when that target unblocked SIGPROF after stopping. Calling glibc backtrace inside the
handler was also unsafe: glibc documents it as AS-Unsafe, including heap and loader locks.

Linux now reserves an unused SIGRTMIN + 6 once for the process lifetime. It refuses an already-owned signal rather
than replacing its handler. Requests are tagged with a monotonically increasing ID; IDs are never reused. The
handler claims the matching request atomically, reads the interrupted PC and OS thread ID, preserves errno and
acknowledges it. A timed-out request is canceled only if the handler has not claimed it; otherwise the sender waits
for the bounded handler to finish before reusing storage. Late canceled requests do nothing. SIGPROF is untouched.

This deliberately changes Linux samples to PC-only. Module classification and runtime self attribution remain;
Linux caller-stack/inclusive attribution is unavailable. Windows still uses suspended-thread x64 unwinding with
SEH protection. This tradeoff should be reviewed explicitly; async-unsafe unwinding was not retained to preserve a
nominal feature. The Windows DLL snapshot is also bounded by its actual allocation if modules appear during enumeration.

References: [glibc backtrace safety](https://www.sourceware.org/glibc/manual/latest/html_node/Backtraces.html),
[Linux signal-handler safety](https://man7.org/linux/man-pages/man7/signal-safety.7.html).

### Frame and GPU boundaries

Stop writes a closing frame mark separately from the event pool. A one-frame environment capture therefore has both
boundaries, including when the normal pool is full. Manual stop closes the final partial frame at stop time.

GPU admission remains open while the writer invokes the registered backend drain. Under the renderer mutex, the
backend submits pending work and collects its timestamp results. New CPU/GPU instrumentation is off during that
drain; pending events retain their generation and can still be written. There is no new query wait in ordinary
capture collection. Stopping can briefly contend with rendering while the drain holds the renderer mutex.

Calibration now queries device plus an advertised host domain together: QPC on Windows, CLOCK_MONOTONIC on Linux.
That host timestamp is correlated to the profiler's cycle counter. Calibration metadata includes the driver-reported
deviation plus the host/counter read bracket. Fallback uses the submission timestamp and is explicitly reported as
estimated. Older captures have unknown alignment. Metadata counters use the existing counter record with bit 8 set
to allow writing them during GPU drain; the low-byte record kind and file version are unchanged.

Reference: [Vulkan calibrated timestamps](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_calibrated_timestamps.html).

### Offline attribution

The old report assigned a whole zone's self time to the frame where it started and selected zones only by start time.
The report now builds nesting before clipping, subtracts direct child intervals to form actual self-time intervals,
and intersects each with frame boundaries. CPU and GPU intervals are clipped to a selected frame window. Empty frame
selections no longer fall back to the entire capture. Counts still count intersecting operations, not fractional calls.

Accumulators remain per-thread, per-observed-frame totals. Their individual intervals cannot be reconstructed, so
they are not split across frames. Open scopes at stop are discarded rather than synthesized. These are format limits.

### Integration

The core was reconciled with upstream test 6cf491e. The backend_execute conflict was resolved by retaining the new
hc_list_begin call inside the profiler's exec accumulator. The control integration was merged separately; its
Makefile conflict retained both control and save-converter tests. F6 now yields to remapped game controls and menus.

The already-open developer-tools work still has older frames/sample commands. Its eventual integration must remove
or adapt those commands; the standalone core and the current prof-command integration do not depend on that work.
Do not submit the complete command-integration branch as the standalone profiler PR.

## Reproduction and verification

The first added regressions failed before implementation: delayed SIGPROF terminated the subprocess, the final
worker total was absent, an old accumulator entered a new capture, a one-frame capture had only one boundary, and
cross-frame CPU/GPU attribution failed. A later nested-child clipping edge test also failed before its correction.

With normal Linux build dependencies installed:

```sh
make test-prof
make test-bind-capture test-vibration test-swkbd
make -j4 build/savagewind
```

`test-prof` currently runs the C recorder test, 21 report tests and 11 subprocess lifecycle tests. The lifecycle suite
includes twenty capture cycles with four concurrent producer threads. Standalone lifecycle suites also passed with
ThreadSanitizer and with AddressSanitizer/UndefinedBehaviorSanitizer. The C recorder harness passed ThreadSanitizer.
Adjacent binding/vibration tests and 80 software-keyboard encoding checks passed. Full Linux runtime builds passed
before and after the upstream merge. These do not establish Linux gameplay or GPU-driver behavior.

Windows core and command-integration runtime builds passed with MSYS2 UCRT64 Clang. Live capture, sampler stability
and controlled off/on overhead verification are recorded separately below when completed. Synthetic ARM64 marker
costs are not a substitute for Windows gameplay measurements.

## Independent review priorities

- Check the admission/busy ordering, slot reuse and TLS destructor interaction, especially on x86-64 and during reboot.
- Check delayed signal cancellation, permanent signal ownership and the intentional Linux PC-only limit.
- Check Vulkan query lifetime, FIFO reuse, renderer locking during stop and frame-boundary interpretation.
- Check the calibration host domains, timestamp wrap, fallback labeling and reported uncertainty.
- Check self-time interval attribution for deep nesting, clipped boundaries and empty selections.
- Keep game-derived data and unrelated optimization changes out of the final PR.

macOS sampler/Metal GPU timing, other GPU vendors, physical hotkey interaction and long-duration soak testing are not
established by the automated tests above. No external viewer, benchmark-walk system or optimization change was added.
