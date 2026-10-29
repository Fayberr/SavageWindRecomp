// Live test input. Commands and frame publication have one writer (the presenter).
#include "control.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

uint64_t renderer_present_count(void);
static const char *const names[] = {
    "a", "b", "x", "y", "lstick", "rstick", "l", "r", "zl", "zr",
    "plus", "minus", "left", "up", "right", "down"
};
static uint64_t until[16];
static bool held[16];
static struct { int32_t x, y; uint64_t until; bool held; } sticks[2];
static _Atomic bool enabled;
static _Atomic uint64_t buttons, axes[2];
static uint64_t published_frame;

static bool duration(const char *s, uint64_t *n, ControlReply *r) {
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (*s < '0' || *s > '9' || *end || errno || !v || v > 1000000) {
        control_fail(r, "frames: use an integer from 1 to 1000000"); return false;
    }
    *n = v; return true;
}
static int button(const char *s, ControlReply *r) {
    for (int i = 0; i < 16; i++) if (!strcmp(s, names[i])) return i;
    control_fail(r, "button: use a b x y l r zl zr plus minus up down left right lstick rstick");
    return -1;
}
static void queued(ControlReply *r, const char *what) {
    control_field_str(r, "queued", what);
    control_field_int(r, "frame", (int64_t)renderer_present_count());
    control_field_int(r, "apply_frame", (int64_t)(renderer_present_count() + 1));
}
static void cmd_button(int argc, char **argv, ControlReply *r) {
    bool hold = !strcmp(argv[0], "hold");
    if (argc < 2 || argc > (hold ? 2 : 3)) {
        control_fail(r, "use %s", hold ? "hold <button>" : "press <button> [frames]"); return;
    }
    int i = button(argv[1], r);
    uint64_t n = 3;
    if (i < 0 || (argc == 3 && !duration(argv[2], &n, r))) return;
    held[i] = hold;
    until[i] = hold ? 0 : renderer_present_count() + 1 + n;
    queued(r, argv[0]);
    control_field_str(r, "button", names[i]);
    if (!hold) control_field_int(r, "frames", (int64_t)n);
}
static void cmd_release(int argc, char **argv, ControlReply *r) {
    if (argc > 2) { control_fail(r, "use release [<button>|all]"); return; }
    if (argc == 1 || !strcmp(argv[1], "all")) {
        memset(held, 0, sizeof held); memset(until, 0, sizeof until);
        memset(sticks, 0, sizeof sticks);
    } else {
        int i = button(argv[1], r);
        if (i < 0) return;
        held[i] = false; until[i] = 0;
    }
    queued(r, "release");
    control_field_str(r, "button", argc == 1 ? "all" : argv[1]);
}
static void cmd_stick(int argc, char **argv, ControlReply *r) {
    if ((argc != 4 && argc != 5) || (strcmp(argv[1], "l") && strcmp(argv[1], "r"))) {
        control_fail(r, "use stick <l|r> <x> <y> [frames], axes from -1 to 1"); return;
    }
    double v[2];
    for (int j = 0; j < 2; j++) {
        char *end; errno = 0; v[j] = strtod(argv[j + 2], &end);
        if (end == argv[j + 2] || *end || errno || !(v[j] >= -1 && v[j] <= 1)) {
            control_fail(r, "stick: use finite x and y from -1 to 1"); return;
        }
    }
    uint64_t n = 0;
    if (argc == 5 && !duration(argv[4], &n, r)) return;
    int i = !strcmp(argv[1], "r");
    sticks[i].x = (int32_t)(v[0] * 32767); sticks[i].y = (int32_t)(v[1] * 32767);
    sticks[i].held = argc == 4 && (v[0] != 0 || v[1] != 0);
    sticks[i].until = n ? renderer_present_count() + 1 + n : 0;
    queued(r, "stick"); control_field_str(r, "stick", argv[1]);
    control_field_num(r, "x", v[0]); control_field_num(r, "y", v[1]);
    control_field_int(r, "frames", (int64_t)n);
}

// Called even when no script exists. Expiry uses presented frames, never script time or poll count.
void control_input_frame(uint64_t frame) {
    if (!atomic_load_explicit(&enabled, memory_order_relaxed)) return;
    uint64_t b = 0;
    for (int i = 0; i < 16; i++) if (held[i] || frame < until[i]) b |= 1ull << i;
    atomic_store(&buttons, b);
    for (int i = 0; i < 2; i++) {
        bool active = sticks[i].held || frame < sticks[i].until;
        uint64_t a = active ? (uint16_t)sticks[i].x | ((uint64_t)(uint16_t)sticks[i].y << 16) |
                     ((uint64_t)sticks[i].held << 32) : 0;
        atomic_store(&axes[i], a);
    }
    published_frame = frame;
}
static int32_t clamp(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
void control_input_apply(uint64_t *b, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    if (!atomic_load_explicit(&enabled, memory_order_relaxed)) return;
    *b |= atomic_load(&buttons);
    if (!lx) return;
    int32_t *x[] = {lx, rx}, *y[] = {ly, ry};
    for (int i = 0; i < 2; i++) {
        uint64_t a = atomic_load(&axes[i]);
        int32_t ax = (int16_t)a, ay = (int16_t)(a >> 16);
        *x[i] = clamp((a >> 32 ? 0 : *x[i]) + ax);
        *y[i] = clamp((a >> 32 ? 0 : *y[i]) + ay);
    }
}
static void cmd_input(int argc, char **argv, ControlReply *r) {
    (void)argv;
    if (argc != 1) { control_fail(r, "use input with no arguments"); return; }
    control_field_int(r, "frame", (int64_t)published_frame);
    uint64_t b = atomic_load(&buttons);
    for (int i = 0; i < 16; i++) control_field_bool(r, names[i], (b >> i) & 1);
    const char *keys[][3] = {{"l.x", "l.y", "l.held"}, {"r.x", "r.y", "r.held"}};
    for (int i = 0; i < 2; i++) {
        uint64_t a = atomic_load(&axes[i]);
        control_field_num(r, keys[i][0], (int16_t)a / 32767.0);
        control_field_num(r, keys[i][1], (int16_t)(a >> 16) / 32767.0);
        control_field_bool(r, keys[i][2], a >> 32);
    }
}
void control_input_init(void) {
    static const ControlCmd cmds[] = {
        {"press", "<button> [frames]", "press a Switch button for 3 frames by default", cmd_button},
        {"hold", "<button>", "hold a Switch button until release", cmd_button},
        {"release", "[<button>|all]", "clear a button, or all live input including sticks", cmd_release},
        {"stick", "<l|r> <x> <y> [frames]", "axes -1..1; timed addition or untimed hold", cmd_stick},
        {"input", "", "show currently applied live input (queued changes apply next frame)", cmd_input},
    };
    control_register(cmds, sizeof cmds / sizeof *cmds);
    atomic_store(&enabled, true);
}
