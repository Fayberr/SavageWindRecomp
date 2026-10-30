# First-open launcher freeze, issue 276

Investigated on 2026-10-30 against upstream `test` commit
`0dece28e9208d76b6cf3089de9991185670fdb86`.
[Issue 276](https://github.com/Revivedx/SavageWindRecomp/issues/276).

The first Settings and Game Files frames synchronously total the installed game
files on the window/event thread. That directory traversal accounts for the
multi-second freeze. Bypassing just the size calculation removes the stall.
The diagnostic build does not implement a production fix.

## Code path

- [Settings, Prelaunch](https://github.com/Revivedx/SavageWindRecomp/blob/0dece28e9208d76b6cf3089de9991185670fdb86/runtime/ui/launcher.mm#L912):
  `prelaunch_tab()` initializes a function-static `gb` with
  `rt_game_files_size(g_root.c_str())` at line 923. This happens before the
  deletion dialog is opened, although only that dialog uses the size.
- [Game Files](https://github.com/Revivedx/SavageWindRecomp/blob/0dece28e9208d76b6cf3089de9991185670fdb86/runtime/ui/launcher.mm#L1425):
  the ready-files branch initializes a separate function-static `files_gb` with
  the same function at line 1429, for the delete row's GB value.
- [Size calculation](https://github.com/Revivedx/SavageWindRecomp/blob/0dece28e9208d76b6cf3089de9991185670fdb86/runtime/paths.c#L186):
  `rt_game_files_size()` visits the recognized `GAME_FILES` entries. `tree_size()`
  recursively enumerates their directories, checks each entry for links, and
  calls `stat()` on each non-link entry. On Windows, the link check also calls
  `GetFileAttributesA()`. This measures filesystem metadata, not game contents.
- Both calls run while `launcher_run()` draws the UI, between iterations of
  its `SDL_PollEvent()` loop. No events or frames can be serviced until the
  calculation returns.

The two independent caches explain why reopening a page is fast. They also mean
opening Settings first does not warm Game Files' cached value, or vice versa.
The experiment below reproduces both stalls in a single process.

## Measurements

Windows 11, Ryzen 7 5700X3D, RTX 3080 Ti, 1280 x 720 Vulkan launcher. The same PC
as the report; no other running game process was stopped by the experiment.
MSYS2 UCRT64 Clang, normal upstream `-O2` runtime build.

### Unmodified size function

A small console harness called the actual `runtime/paths.c` function three times
per path, without launching SDL or the renderer. Each populated call returned
15,157,992,803 bytes.

| Path | Call 1 | Call 2 | Call 3 |
| --- | ---: | ---: | ---: |
| Installed extracted folder, direct X: path | 10,687 ms | 11,013 ms | 11,128 ms |
| Existing C: scratch-root junction to that folder | 15,964 ms | 16,254 ms | 20,527 ms |
| Empty folder | 11.4 ms | 12.5 ms | 9.5 ms |

The direct-path result matches the issue's approximate 11-second hand timings.
Repeated calls remain slow without the UI's static caches. Absolute times vary
with the path and system load; no storage, antivirus or Windows CRT subroutine
has been independently profiled as the cause of that variation.

### Instrumented upstream launcher

The isolated runtime was built from the pinned upstream source with timing and
page-selection instrumentation, without the Saves tab, control endpoint or other
local feature branches. Each test used a separate data root and a fresh process.
The populated roots had an `extracted` junction to the same installed files.

The probe switched pages at these frame numbers:

```text
1 Main
40 Settings, Prelaunch tab
80 Main
100 Game Files
140 Main
160 Settings again
180 Game Files again
200 screenshot and exit
```

To isolate UI drawing from game-library compatibility, the diagnostic sequence
cleared the readiness status before choosing the first page. This also allowed
the empty-folder case to exercise the same ready-files branch. No game library
was loaded, and no setup or deletion action was activated. This was a controlled
UI experiment, not an unmodified end-to-end game start.

`ui_ms` spans from immediately after `ImGui::NewFrame()` to immediately before
`ImGui::Render()`. These are CPU UI construction times, not full presented-frame
or input-to-display latency measurements.

| Case | First Settings | First Game Files | Settings reopened | Game Files reopened |
| --- | ---: | ---: | ---: | ---: |
| Populated, normal scan | 23,586 ms | 24,112 ms | 1.393 ms | 3.584 ms |
| Populated, size function returns immediately | 1.689 ms | 4.330 ms | 1.948 ms | 6.187 ms |
| Empty, normal scan | 14.844 ms | 14.642 ms | 1.802 ms | 1.784 ms |

The normal populated case logged:

```text
[probe] size bytes=15157992803 elapsed_ms=23568.213
[probe] frame=40 page=1 before_ui_ms=0.686 ui_ms=23586.050
[probe] size bytes=15157992803 elapsed_ms=24097.462
[probe] frame=100 page=2 before_ui_ms=0.446 ui_ms=24111.706
```

More than 99.9% of each slow UI construction interval is inside the size
function. The populated bypass used the same files, renderer and fonts; its
displayed size was deliberately zero. This is a diagnostic control only.

For the normal populated process, the first frame's interval before UI drawing,
including font/backend frame preparation, was 22.804 ms. At the first Settings
open it was 0.686 ms. A seconds-long first font-atlas build does not explain
these measured freezes.

All three 200-frame launcher runs exited with status 0 and wrote screenshots.
Total process times were 51.75 s for the normal populated case, 4.13 s for the
populated bypass, and 4.03 s for the empty case. The populated screenshot and
bypass screenshot were inspected. Timing instrumentation, page overrides and
the bypass were removed from the tracked runtime files after the experiment.

## Windows scan-speed follow-up

A standalone prototype reuses the size and reparse-point attributes provided by
Windows directory enumeration, avoiding a separate link check and `stat()` for
every child. The existing `GAME_FILES` list is reused unchanged. Only the known
top-level paths need `GetFileAttributesExA()`; directory children use
`FindFirstFileExA(..., FindExInfoBasic, ...)` and `FindNextFileA()` metadata.
[Windows directory-entry fields](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-win32_find_dataa).

Two passes per path alternated the actual upstream size function, the native
prototype with default flags, and the same prototype with
`FIND_FIRST_EX_LARGE_FETCH`. No threading or application-level size cache was
used. All populated totals matched exactly: 15,157,992,803 bytes, with the native
scanner visiting 41,067 files and 874 directories.

| Path | Pass | Upstream | Native enumeration | Native, large fetch |
| --- | ---: | ---: | ---: | ---: |
| Direct X: folder | 1 | 11,920 ms | 169 ms | 164 ms |
| Direct X: folder | 2 | 11,727 ms | 174 ms | 185 ms |
| C: scratch-root junction | 1 | 16,468 ms | 320 ms | 259 ms |
| C: scratch-root junction | 2 | 16,750 ms | 253 ms | 257 ms |

Default native enumeration was about 67 to 70 times faster on the direct path.
Large fetch did not consistently improve these runs, so the basic change does
not require it. This measures a stable, already-accessed tree on this machine;
it is not a cold-boot, network-drive or cross-machine benchmark.

A separate fixture returned the expected 40 bytes in both implementations:
two ordinary files, a dot-prefixed filename, nested directories, an unrelated
top-level folder excluded by `GAME_FILES`, and a junction to an outside folder
excluded by the reparse-point check. The empty fixture returned zero. Synthetic
metadata also checked a file size above 4 GiB and skipping a reparse entry.
This does not exhaustively test permissions, concurrent mutations, Unicode paths
or every reparse-point type.

The optimized scanner remains an ignored benchmark prototype under
`build/probe/fastscan/`; the production runtime is unchanged. The speedup itself
is a small Windows-specific scanner change. Background scanning and a shared
cache are still useful to keep the window responsive on slower filesystems.

## Recommended fix and verification

Run size enumeration on a worker and expose a shared cached result to both
pages. Keep the current size unknown while the calculation runs, so neither
page waits for filesystem traversal. Publish results safely and discard stale
results after a new setup, removal of DLC, deletion, or a root change. A scanner
must retain the existing recognized-file and no-link-following behavior.

Starting the same synchronous scan earlier would move the freeze to startup.
Sharing the existing synchronous caches alone would remove the duplicate scan
but leave the first freeze. Deferring Settings' scan until its delete dialog is
open would also leave Game Files and the dialog vulnerable.

Windows directory-entry metadata reuse substantially reduced the scan cost in
the follow-up above. Asynchronous operation should not depend on a particular
disk being fast.

For a fix, verify both page orders, first and repeat opens, empty and populated
roots, and that event processing continues while a deliberately slow scan is
running. Verify cache invalidation during setup/deletion/DLC changes and that
an old worker cannot publish into a new root/session. Check worker shutdown and
thread-creation failure. Linux and macOS behavior remains unmeasured.

## Local evidence

The investigation worktree is `SavageWindRecomp-wt/issue-276`, branch
`investigate/issue-276-first-open`. Ignored evidence under `build/probe/` contains
the original upstream source archive, standalone harness, diagnostic patch,
instrumentation generator, Windows build/launch scripts, logs and screenshots.
The same Windows project workspace holds its isolated build. None of these
runtime artifacts or game files belong in a commit.

No upstream comment, issue edit, PR, release build or production change was
published as part of this investigation.
