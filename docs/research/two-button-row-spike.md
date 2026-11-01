# Two native buttons in an Options row: spike

2026-11-01, BotW 1.9.0. Branches: `fleet/s209` and `spike/two-button-row`.

**Result so far: implemented and Windows-built, not proven on a live Options screen.**
The native game screen's geometry, focus animations and sound still need Fabian's check.
An existing `a4-act` game session prevented a second gameplay launch and was left untouched.

## Implemented

Only the first three action rows on P_BINDS, Interact (0), Jump (2), Attack (3), use two cells.
The existing choice control is the keyboard/mouse cell; the ordinary control is the controller cell.
Both remain registered native controls. Choice arrows are hidden. Both enabled/disabled text copies
use the game's message text setter. No ImGui controls or Nintendo assets were added.

The buttons are narrowed to half width, moved apart, and their text gets inverse horizontal scaling
to retain native font proportions. The label is narrowed too. This geometry is a candidate,
not a visually approved layout. Other rows and pages retain their existing path.

Native select, state, confirm, initial-focus and update routing now recognize both cells.
Left/right changes column, up/down retains it, mouse hit testing uses two disjoint native panes,
and confirm starts BIND_DEV_KEY or BIND_DEV_PAD capture. Mouse clicks request the native
Decide animation before its confirmed callback, rather than calling capture directly. Captures redraw both cell texts.
A sidecar stores state without enlarging the game's row object.

All 13 new sites are one strict optional group. A missing, ambiguous or wrong-address match disables
the group even on a known build, preserving the existing single-button rows. Unknown row shapes
also keep their original redraw. `SAVAGEWIND_TWO_BUTTON_ROWS=0` disables the runtime experiment.

## Evidence and actual checks

- Every new signature uniquely matches the supplied 1.9.0 main NSO at its listed KNOWN address.
  Every emitted hook's replaced opcode was checked against that binary; generated hook C passed syntax checking.
- Verified pane translation `+0x30`, scale `+0x48`, size `+0x50/+0x54` and dirty bit 4 at `+0x58`.
  Native Pane calculation at `0x70000276a0` rebuilds the matrix at `+0x70` and propagates to children
  (`0x7000027744..0x7000027758`, `0x7000027ad0..0x7000027adc`, `0x7000027bb0..0x7000027bc8`).
  Cached world matrices are never edited. No BFLYT/archive patch was necessary for this candidate geometry.
- Linux and Windows: `make test-actions test-bind-capture test-pc-controls` pass.
  Windows also passes `test-input-focus`, `test-gyro`, `test-two-button-rows`, the 45 game-site checks,
  and the strict-group failure tests. The new integration test exercises the actual Options hooks and
  real capture engine with synthetic rows, including wrong-device rejection and CPU-state preservation.
- Windows release executable, configuration executable and frozen generator build pass.
  This includes the edited `.mm` file. Build log: `X:\.Development\swr-wt\row-spike\build\row-spike-build.log`.
- `vibe-check runtime/ui/game_options.mm` reports no findings; `git diff --check` passes.
- Live UI, native hover/confirm sounds, long labels, scrolling, repeated directions, reset/reopen and
  physical controller input remain unverified. Host tests do not establish these properties.

## Fabian: launch and inspect

The isolated install is `C:\Users\derfa\swr-check\row-spike`; its source checkout is
`X:\.Development\swr-wt\row-spike`. Its ini/save were copied once from the existing play folder.
It owns its ExeFS and generated game code; RomFS/AOC directory junctions share installed game resources.
The original action-binding checkout and play folder were not changed.

1. Close the other Savage Wind game normally. The play script refuses to interrupt an existing game.
2. In `pwsh`, run `C:\Users\derfa\row-spike-task.ps1 -Mode play`.
3. Open the game's Options, then **Key bindings**. Inspect the first three action rows.
4. Move left/right, then up/down; hover/click each cell. Confirm the left cell and try a controller
   button before a key; confirm the right cell and try a key before a controller button. Esc cancels.
   Compare highlight and sounds with an unchanged row/page; return and reopen the page.
5. Logs are in the isolated folder: `play-stderr.log`, `play-stdout.log`, and `play-task.log`.
   `[row-spike]` lines identify configured buttons, selections and capture devices.
   `-Mode shot` captures the interactive desktop to `row-spike\desktop.png`; no game screenshot is claimed here.

Build/install/setup scripts are committed under `tools/windows/row-spike-*.ps1`, with copies in
`C:\Users\derfa`. The setup mode builds only this installation's game code and then exits.

## Recommendation

**Go with changes, provisionally.** Static structure and host routing support the approach;
live feasibility is still unproven, so do not roll this out to the full page yet.
After the native screen passes inspection, allow roughly 2 to 4 focused days for a full page:
final widths and long labels, disabled controller cells, native navigation/scroll polish and device QA.
Column headers likely need two added text panes through the existing in-memory Common archive rebuild,
scoped to P_BINDS, plus space above the list. That remains a feasibility note, outside this spike.
