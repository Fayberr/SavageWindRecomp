# Control endpoint (testing and development)

A small command server inside the game, for people and for scripts and AI agents. It is for testing and
development only, and it is off unless you ask for it:

```
SAVAGEWIND_CONTROL=7777 ./savagewind        (Windows: set the variable, then start savagewind.exe)
python tools/swctl.py state
```

It listens on `127.0.0.1` only (not reachable from the network). Nothing in it is a player feature.

## Using it

`tools/swctl.py` needs only Python 3.8 or later, with no extra packages.

```
swctl help                 every command with its arguments and a one line description
swctl state                frame number, fps, hour of day, ...
swctl hour                 show the hour of the day
swctl hour 18              hold the hour of the day at 18:00 (swctl hour free: let it run again)
swctl shot out.png         save a screenshot (the path is on the machine the game runs on)
swctl --json state         raw reply, for scripts
swctl --port 7777 ...      other port (default: SAVAGEWIND_CONTROL, else 7777)
swctl --host 127.0.0.1 ... other host (default: 127.0.0.1)
swctl --timeout 5 ...      connect and reply timeout in seconds (default: 5)
swctl --wait 10 state      retry connecting for up to 10 seconds (default: 0)
```

Options go before the command. The default port uses `SAVAGEWIND_CONTROL` only when it is numeric, else 7777.
Ports must be from 1 to 65535. `--wait` retries only connecting, so it never sends a command twice.

Output is plain `key: value` lines, with dotted keys for nested objects and one line per object in a list.
`help` prints an aligned command table. Errors go to stderr. `--json` prints only the original reply line to
stdout, including when the game answers with an error.

Exit code 0 on success, 1 when the game answered with an error, 2 for connection failures, timeouts or invalid
replies. A reply needs its final newline, even if the game closes the connection. With no command, the client
prints usage and suggests `help`.

## Protocol

TCP, text, one request per line, one reply per line, UTF-8, `\n` line ends. Several requests may be sent over one
connection; replies come in the same order.

Request: the command and its arguments separated by spaces. Double quotes keep spaces inside one argument, and
`\"` and `\\` work inside them.

```
hour 18
shot "C:\\Users\\me\\out 1.png"
```

Reply: one JSON object.

```
{"ok":true,"data":{"hour":18.0}}
{"ok":false,"error":"hour: expected a number from 0 to 24 or \"free\""}
```

A command that gives no data answers `{"ok":true,"data":{}}`. Commands run once per presented frame, on the thread
that presents, so they never run in the middle of a frame; a reply can take up to one frame (and a command may wait
for more, see its help). Unknown commands answer an error that says so.

Always there: `help` (data: `commands`, a list of `{"name","args","help"}`), `ping` (data: `pong` = true) and
`exit` (ends the game; no data).

## Commands

| command | what it does |
|---|---|
| `help` | list the commands |
| `ping` | check that the game answers |
| `state` | frame number, uptime, fps over the last second, hour of day |
| `hour [<0-24> \| free]` | show the hour of the day; with a number, hold it there (`free` lets it run again) |
| `pos` | Link's position (x, y, z) and stamina; an error before a save is loaded |
| `teleport <x> <y> <z>` / `teleport status` / `teleport forget` | moves Link. `pos` reads a copy the game rewrites every frame, so the write goes to the real position, which is reached through a pointer chain from `PlayerInfo` (found once with `mem path`, checked against `pos` each time): no search, the move happens in the frame the command runs and `verifying` ends after about 0.15 s. If the chain does not match (another game version) it falls back to a search that never blocks a frame: a background scan (about 0.3 s) lists every place holding Link's x/y/z, the list is cut to the real one in four short rounds by nudging four groups of candidates by 0, 0.05, 0.10 and 0.15 (centimetres, undone at once) and reading how far `pos` moved, then the target is written to the one left. In the fallback it replies with `state: scanning`; poll `teleport status` (`scanning`, `probing`, `testing`, `verifying`, `done`, `failed`, plus `message`, the `address` and the current x/y/z). Found addresses are reused and searched again if they go stale. Stand still when you start; do not teleport while riding, in a cutscene or loading. A target below the ground makes him fall. `forget` drops the saved address |
| `shot <file>` | write a PNG of the frame being presented (Vulkan only for now); answers once the file is written |
| `get [key]` | list all current settings or one; keys are `Settings` field names, plus `window_size`, `fps` (`fps_limit`), `left_stick_deadzone` and `right_stick_deadzone`; enum values use lowercase names, languages show indices; values are configured values (environment overrides and hardware fallbacks still apply) |
| `set <key> <value>` | change a setting in memory; answers with the new value and `applied: live` or `needs_restart`; examples: `set upscaler fsr3`, `set antialias taa`, `set vsync off`, `set fps 120`; enums also accept integer indices, booleans accept on/off, true/false or 1/0; unavailable backend options are rejected (FSR3 upscaling needs a build with FSR3 and `SAVAGEWIND_FSR3=1` at startup); `window_size` accepts `WIDTHxHEIGHT` or `default` and queues the menu's window apply path (the display may choose a supported size); individual window dimensions, LUT selection and bookkeeping flags are read-only here; floats use struct units (volume 0..2, sensitivities as factors, deadzones 0..0.5), sharpening uses 0..10, exposure uses tenths of EV, other colour controls use percent; languages accept indices, names or folders (`auto` for text, `text` for voices), and need an in-game restart; precompile_shaders affects the next launcher setup; no settings file is written, but later menu saves may persist these changes; no save flag because file I/O would block the presenting thread |
| `frames [seconds]` | frame intervals ending in the last seconds (default 5, max 60): count, covered_s, capacity_limited, fps_avg, frame_ms_avg/p50/p95/p99/max (nearest rank), over_33_ms, over_50_ms; includes pacing and hitches, not GPU-only time. Integration required: declare `void control_prof_frame(uint64_t frame);` and add `control_prof_frame(frame);` at the start of `control_game_frame` in runtime/control_game.c. Until wired, returns a hook-missing error. Keeps 32768 intervals; shorter history is reported as collected. |
| `sample start <seconds> [interval_us]` | Windows only: start a background stack capture for 0.001-3600 seconds, interval 100-1000000 integer microseconds (default 1000); overwrites build/samples.txt after collection, relative to the game's working directory. Create a writable build folder first; analyse with tools/profile_samples.py. Refuses overlapping captures, including SAVAGEWIND_SAMPLE captures. |
| `sample status` | status and path: idle, starting (including an env-var delay), sampling, writing, done, or a failure with recovery advice; unsupported on other platforms. Start acknowledges the worker, not a completed file. |
| `vkprof [0\|1\|2]` | returns an error explaining runtime switching is unsafe: startup query pools and shared worker flags. Restart with SAVAGEWIND_VK_PROF=1 (detailed) or 2 (coarse); unset it to disable. Timers appear in the fps log; SAVAGEWIND_HITCH_MS sets the hitch threshold. |
| `gpuprof` | returns an error: no nonblocking runtime trigger, reporting flushes and waits for the GPU. Restart with SAVAGEWIND_GPUPROF_AT=<frame> and read [gpuprof] in the log. |
| `mem regions` | readable OS mappings inside the guest window, as `region.N.start` and `region.N.size` in bytes; `scale` is big (at least 64 MiB) or small, `likely_heap` flags overlap with the guest heap window; indices match scan filters but may change when mappings change; includes unused reserved space where readable |
| `mem read <addr> <len> [u8\|u16\|u32\|u64\|s32\|f32\|f64\|hex]` | read 1-4096 bytes (hex default); typed lengths must be multiples of the type width; decimal or `0x` addresses, guest byte order |
| `mem write <addr> <type> <value>` | write one numeric value, using the read types except hex; rejects inaccessible ranges and values outside the type's range |
| `mem scan <type> <value> [tolerance] [region=<n>[,<n>...]] [from=<addr> to=<addr>] [resident=1]` | returns `started: true` at once; one background worker (below-normal priority) scans the writable guest mappings (a `region=` filter also allows read-only ones), numeric types only, naturally aligned; optional float tolerance (absolute, default 0); region indices come from `mem regions`, `from`/`to` is start-inclusive, end-exclusive, filters intersect; keeps at most 100000 candidates. Memory is read in place (AVX2 compare where the CPU has it; about 11 GB/s on the test PC, a full 3.2 GB scan in about 0.25 s). `resident=1` reads only pages resident in RAM; it was slower in tests (0.87 s vs 0.35 s) and can miss data the OS has trimmed out of RAM, so it is off by default. All-zero pages are skipped when 0 cannot match. Values may tear while the game writes them; a page that faults is left out and counted in `skipped_bytes` |
| `mem scan status` | poll without waiting: `scanned_mb`, `total_mb`, `percent`, `candidates`, `done`, `skipped_bytes`, and where the time went: `elapsed_s`, `mb_per_s`, `regions_ms` (mapping the regions), `resident_ms` (asking which pages are in RAM, `resident=1` only), `scan_ms` (reading and comparing), `nonresident_mb`, `zero_mb`, `compared_mb`, `runs`, `faults`; when done also the candidates, `capped` and `cancelled` |
| `mem scan continue` | compatibility alias for `mem scan status`, including after completion; does not perform scan work |
| `mem path <root> <target> [depth=1..6] [window=16..65528]` | find how to reach `target` from the object at `root` without searching memory: breadth first over every pointer-looking value in the first `window` bytes (default 0x400) of each object up to `depth` (default 4) levels, on a background thread (below-normal priority; about 2M objects at most). Poll `mem path status`: `state`, `nodes`, and when done `ms`, `truncated`, `paths` and `path.N` = comma separated offsets (read the pointer at root+o1, then at that+o2, ... and add the last offset to the final pointer; shortest chains first, up to 32). Heap addresses move between runs but object layouts do not, so a chain found once works on every boot |
| `mem rescan <changed\|unchanged\|increased\|decreased\|equal <value>\|near <value> <tol>>` | narrow the current candidates; compares against their last retained values and updates those values; changed/unchanged use the current tolerance, equal uses zero, near sets an absolute nonnegative tolerance |
| `mem rescan continue` | continue a partial rescan; each call works for about 1 ms on the command thread using only candidates; OS call latency can exceed that budget; finish it before starting another rescan |
| `mem list [n]` | show stored candidate addresses and sampled values, default 20, maximum 1000; completed scan status and rescan replies show the first 20, `count`, `partial`, and `capped` (candidate limit reached); unavailable during an initial scan |
| `mem reset` | discard the scan session; while a worker runs, requests cancellation and returns `cancelling: true` without waiting; poll status until done, then reset to discard; during an initial scan only regions, scan status/continue and reset are accepted; one session is shared by all clients; restart scans after a game reboot; host allocations outside the guest window are excluded |
| `press <button> [frames]` | queue a Switch button for 1-1000000 presented frames (default 3); buttons: a b x y l r zl zr plus minus up down left right lstick rstick; merges with existing buttons; replies include queued action, frame and apply_frame |
| `hold <button>` | queue a button held until release; merges with existing buttons |
| `release [<button>\|all]` | clear a live button; omitted argument or all clears all live buttons and sticks; scripts remain active |
| `stick <l\|r> <x> <y> [frames]` | axes from -1 to 1, positive y up; timed input adds with clamping for 1-1000000 frames; without frames holds that stick over other input until stick l/r 0 0 or release all; returns to existing input on expiry; frames: 0 means untimed |
| `input` | currently applied live buttons and stick axes, plus publication frame; queued changes apply at the next input frame boundary, before scripts run, and may not appear here yet |
| `exit` | end the game |
| `swbench.py run [--port N] [--hour H] [--set key=value ...] [--warmup S] [--duration S] [--shot FILE] [--label NAME] [--out FILE]` (client tool) | wait up to 30 seconds for startup (`--wait S` overrides), warm up for 5 seconds and sample for 30 seconds by default; record FPS, frame-counter rates, hour and optional `frames` data; print a summary and write JSON (default: `result.json`); fractional durations include a shorter final sample |
| `swbench.py compare a.json b.json` (client tool) | compare summaries and percentage changes (second vs first); warn about different hours; use the largest sample range as a noise heuristic, not a significance test |

More come as they are needed (weather, settings, profiler, camera, warp). To add one: write a handler and add it to
the table in `runtime/control_game.c`; `help` and this list pick it up from there.
