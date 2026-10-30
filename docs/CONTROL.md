# Control commands

One list of named commands that more than one front end can run. Today that is a local socket, for scripts, AI agents
and `tools/swctl.py`, and the in-game console. Each command is either player safe (it only reads state or copies text, it cannot break the game)
or developer only.

## Who may run what

| | player safe commands | developer commands |
|---|---|---|
| normal start | yes | no |
| `SAVAGEWIND_DEV=1` | yes | yes |
| `SAVAGEWIND_CONTROL=<port>` | yes | yes |

A normal start has no socket and no developer commands: they are not listed by `help`, not run, and answer
`Unknown command`. `SAVAGEWIND_CONTROL=<port>` also opens the socket, `SAVAGEWIND_DEV=1` only unlocks the developer
commands in the in-game console.

The socket listens on `127.0.0.1` only (not reachable from the network).

```
SAVAGEWIND_CONTROL=7777 ./savagewind        (Windows: set the variable, then start savagewind.exe)
python tools/swctl.py state
```

A command is player safe when its entry in the table has the `CONTROL_PLAYER` flag (`runtime/control.h`). Leave the flag
off for anything that writes memory or files, moves things or ends the game.

## The in-game console

The same commands can be typed in the game: press `` ` `` (the key left of 1, which is `^` on a German keyboard) or F12.
The game gets no input while the console is open; `` ` ``, F12 or Esc closes it. Up and Down walk through what you typed,
Tab completes a command, Ctrl+V pastes from the system clipboard, `clear` empties the screen. The console is in English,
like the command names and replies it shows.

It runs the same commands as the socket, with the same rights: a normal start offers only the player safe commands
below, with no socket. Text a command asks to copy (`pos`, `report`) goes to the system clipboard. `exit` closes the
game the way the window's close button does.

## Player safe commands

| command | what it does |
|---|---|
| `help` | the commands you can use |
| `version` | game version, operating system and architecture |
| `report` | one block for an issue: version, graphics card, processor, memory, frame rate, position and every setting (copied to the clipboard in the console) |
| `pos` | Link's position (x, y, z) and stamina, and `place`, a line like `swr:1 x=-1234.56 y=250.00 z=789.01 yaw=90.0 pitch=-10.0 hour=18.50`, to say where a bug is. Yaw and pitch work in any AA mode, the hour when known. An error before a save is loaded |
| `camera` | where the world camera is (`x y z`) and which way it looks (`dx dy dz`, unit vector; `yaw` and `pitch` in degrees: yaw 0 looks along +z and 90 along +x, pitch positive looks up). Read only, works in any AA mode: it uses the renderer's TAA view matrix when there is one, otherwise the game's stored camera. The stored camera reports its stored eye position, which can differ from the drawn one by about two metres; the direction is the same. An error before a world is loaded |
| `get [key]` | the current settings, or one; keys are `Settings` field names, plus `window_size`, `fps` (`fps_limit`), `left_stick_deadzone` and `right_stick_deadzone`. Enum values use lowercase names; values are the configured ones (environment overrides and hardware fallbacks still apply). Read only |
| `state` | frame number, uptime, fps over the last second and the hour of the day (left out before a world exists) |
| `exit` | end the game: the console closes the window the normal way, the socket exits at once |

`ping` (data: `pong` = true) answers on the socket in developer mode.

## Using swctl

`tools/swctl.py` needs only Python 3.8 or later, with no extra packages.

```
swctl help                 every command with its arguments and a one line description
swctl state                frame number, fps, hour of day, ...
swctl pos                  where Link is
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
get fps
camera
```

Reply: one JSON object.

```
{"ok":true,"data":{"fps":60}}
{"ok":false,"error":"Unknown setting: x"}
```

A command that gives no data answers `{"ok":true,"data":{}}`. Commands run once per presented frame, on the thread
that presents, so they never run in the middle of a frame; a reply can take up to one frame. Unknown commands answer
an error that says so.

Always there: `help` (data: `commands`, a list of `{"name","args","help"}`) and `exit` (ends the game; no data).

## Adding a command

Write a handler in `runtime/control_game.c`, answer with `control_field_*()` for the data and `control_fail()` for an
error, and add it to the table at the bottom with `CONTROL_PLAYER` if it is player safe. `help` and this page pick it up
from there. `control_text()` and `control_copy()` are for front ends that print text or use a clipboard, and are
ignored by the socket.

`make test-control` covers the command layer: who may run what, the queue and the replies.
