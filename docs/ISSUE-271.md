# Issue 271: horse names use the wrong output encoding

Investigation date: 2026-10-30.
Issue: [Horse naming error Dueling peaks Stable](https://github.com/Revivedx/SavageWindRecomp/issues/271).
Source examined: `test` at `0dece28e9208d76b6cf3089de9991185670fdb86`.

The software-keyboard HLE writes UTF-8 into a buffer that BotW 1.9.0 reads as UTF-16LE.
This explains the reported full name in the entry field and single final letter in the game's confirmation.
The source defect and the caller's interpretation are confirmed in isolation. No live horse-registration run
or rendering verification was performed, and this investigation does not change the runtime.

## Evidence

The report's entry screenshot contains `Talon`; its confirmation screenshot contains `n` in both the horse
panel and dialogue. The [attached log](https://github.com/user-attachments/files/34164552/savagewind.log),
line 1190, records the complete submitted name:

```text
[swkbd] name entry (max 9, initial ""): entered "Talon"
```

The same log records other complete names, including `Nintendo` and `AA`. The text reaches the HLE intact.

In `runtime/hle/swkbd.c`, `swkbd_show` clears the guest output buffer and copies the UI's UTF-8 text with
`snprintf`. It never reads `ShowKeyboardArg + 0x3bd`, the `IsUseUtf8` flag. The comment claiming that the game
reads the answer as UTF-8 is wrong for this caller. The field and adjacent offsets are documented by the
[Switchbrew software-keyboard research](https://switchbrew.org/wiki/Software_Keyboard#KeyboardConfig).

Local inspection of the user's own 1.9.0 executable and SDK established that:

- The game selects a keyboard preset, sets the name limit to 9, and does not enable UTF-8 output.
- Both presets used by its keyboard wrapper leave `IsUseUtf8` zero. Running those SDK preset routines in
  Unicorn with only the memory-fill import substituted confirms this.
- After `ShowKeyboard` succeeds, the caller counts 16-bit units until a zero unit, copies twice that count
  in bytes, and appends a 16-bit terminator. It does not convert UTF-8.
- Running that actual post-keyboard copy routine in Unicorn with the C probe's returned bytes yields
  `U+6154, U+6F6C, U+006E` for `Talon`.
- The same caller preserves all five letters when given the synthetic expected UTF-16LE bytes instead.

No instructions, game files, generated code or private binary-analysis scripts are included here.
The executable's main build ID starts with `cd57b23fa4bbad65`, the project's known 1.9.0 identifier.

## Why the last letter survives

The current HLE returns these bytes for `Talon`:

```text
54 61 6c 6f 6e 00 00 00 ...
```

The game pairs them into UTF-16LE units:

```text
6154 6f6c 006e 0000
```

`006e` is `n`; the first two units are unrelated CJK characters. Missing glyphs for those units would leave
only `n`, matching the screenshot. Their exact rendering was not independently tested.
The correct UTF-16LE buffer is:

```text
54 00 61 00 6c 00 6f 00 6e 00 00 00
```

A one-letter ASCII name happens to work because the cleared buffer supplies the needed zero bytes. Even-length
ASCII names do not retain a final ASCII unit. These are encoding consequences, not a word-filter rule.
The source and caller evidence point to the keyboard boundary; no change to graphics settings is indicated.

## Reproduce without game data

`tools/probe_swkbd_encoding.c` includes the actual `runtime/hle/swkbd.c` and substitutes only the UI prompt.
It uses synthetic argument structures and guard bytes around the output. From the repository root on Linux:

```sh
clang -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer tools/probe_swkbd_encoding.c -o /tmp/probe_swkbd_encoding
/tmp/probe_swkbd_encoding
```

Observed on Linux ARM64 with Clang 18:

- UTF-8 requests: 7 of 7 byte comparisons match.
- UTF-16LE requests: 1 of 7 matches, the accidental single-letter case. `Talon`, `AA`, `Nintendo`, an accented
  name, a CJK character and a surrogate pair all mismatch.
- Output guard bytes stay intact and AddressSanitizer/UndefinedBehaviorSanitizer report no errors.

The probe exits zero after printing its observations. `MISMATCH` demonstrates the bug; a zero exit status
does not mean the keyboard is correct. It is not wired into the normal test or production build.

## Related findings and fix scope

The direct fix belongs in the keyboard HLE: convert the UI's UTF-8 text to UTF-16LE when the flag is zero,
retain UTF-8 when requested, respect the byte capacity, terminate correctly, and never split an encoded
character or surrogate pair. A universal switch to UTF-16 would leave the flag contract incomplete.

Two nearby problems need consideration when preparing that change:

- The HLE reads the work-buffer pointer at `+0x4d0`. The inspected 1.9.0 caller and SDK use `+0x518`.
  A synthetic initial `Talon` at the latter offset reaches the UI as empty. The report's initial strings
  are already empty, so this is not the cause of its corrupted submitted name. Other supported versions'
  layouts must be checked before changing the offset universally.
- Initial non-ASCII UTF-16 units become `?`, and the UI uses `strlen` and a byte-index truncation for its
  length limit. Valid UTF-8 conversion and appropriate length accounting are needed for non-ASCII names.

Cancellation also currently returns success unconditionally; the inspected SDK distinguishes cancellation.
That is adjacent behavior, not an explanation for this confirmed submission.

Before submitting a fix, verify horse registration, the confirmation, the horse list and save/reload in 1.9.0.
Cover odd/even and maximum-length ASCII names, Unicode and surrogate pairs, short output buffers, existing
initial text, cancellation, repeated prompts and both keyboard/controller entry. The synthetic probe and
isolated caller execution do not replace this live acceptance check.
