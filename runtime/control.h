// (test and development only, never a player feature) Control endpoint: a line based command server on the local
// machine, so a person (tools/swctl.py) or a script / AI agent can look at and steer a running game. Off unless
// SAVAGEWIND_CONTROL=<port> is set. Protocol and command list: docs/CONTROL.md.
//
// Threads: the server thread only reads lines and queues them. Every command runs in control_frame(), once per
// presented frame on the thread that presents, so a command never runs in the middle of a frame.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct ControlReply ControlReply;  // what a command answers with (opaque; use the functions below)

// A command handler. argv[0] is the command name, argv[1..argc-1] the arguments (quotes already resolved).
// Answer with control_ok() / control_fail() (at most one; nothing counts as control_ok with no data).
typedef void (*ControlFn)(int argc, char **argv, ControlReply *r);

typedef struct {
    const char *name;  // "hour", "shot", ...; subcommands are plain arguments: "set hour 12" is name "set"
    const char *args;  // usage of the arguments for `help`, e.g. "<hour 0-24 | free>" ("" if none)
    const char *help;  // one line: what it does
    ControlFn fn;
} ControlCmd;

// Register a table of commands (once, e.g. from control_game_init); the names must be unique.
void control_register(const ControlCmd *cmds, int count);

// Answers. A reply is a JSON object: control_field_*() add members to "data" (strings are escaped for you).
void control_field_str(ControlReply *r, const char *key, const char *value);
void control_field_int(ControlReply *r, const char *key, int64_t value);
void control_field_num(ControlReply *r, const char *key, double value);
void control_field_bool(ControlReply *r, const char *key, bool value);
void control_fail(ControlReply *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));  // {"ok":false,"error":...}

// Start the server if SAVAGEWIND_CONTROL is set (does nothing otherwise). Called once at startup.
void control_init(void);
// Once per presented frame: run the queued commands. Cheap when nothing is queued.
void control_frame(uint64_t frame);

// The game's commands (state, hour, shot, ...), registered by control_init() through this.
void control_game_init(void);
// Called by control_frame() at the start of every frame, also when no command is queued, but only when the
// endpoint is on (the game side keeps its fps average with it).
void control_game_frame(uint64_t frame);

// Live input hook: frame publication on the presenter; apply may run on guest threads.
// A NULL lx requests buttons only. Otherwise all four axis pointers must be valid.
void control_input_frame(uint64_t frame);
void control_input_apply(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
