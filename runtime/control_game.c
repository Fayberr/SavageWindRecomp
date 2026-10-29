// (test and development only) The game's commands of the control endpoint (runtime/control.h, docs/CONTROL.md):
// state, hour, shot. Add a command here by writing a handler and putting it in the table at the bottom.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "control.h"
#include "rt.h"
#include "game_sites.h"

uint64_t renderer_present_count(void);  // presented frames (the backend)
bool control_shot_now(const char *path);  // the image of the frame being presented -> PNG (the backend; false: can't)
void test_warp_set_hour(float hour);  // runtime/services/test_warp.c
float test_warp_hour(bool *held);

// ---- fps over the last second, kept every frame
#define FPS_WINDOW_NS 1000000000ull
static uint64_t win_start_ns, win_start_frame;
static double fps_now = -1;
static uint64_t boot_ns;

void control_prof_frame(uint64_t frame);  // runtime/control_prof.c
void control_tp_frame(uint64_t frame);    // runtime/control_tp.c

void control_game_frame(uint64_t frame) {
    control_prof_frame(frame);
    control_tp_frame(frame);
    uint64_t now = plat_now_ns();
    if (!boot_ns) boot_ns = now;
    if (!win_start_ns) { win_start_ns = now; win_start_frame = frame; return; }
    if (now - win_start_ns >= FPS_WINDOW_NS) {
        fps_now = (double)(frame - win_start_frame) * 1e9 / (double)(now - win_start_ns);
        win_start_ns = now;
        win_start_frame = frame;
    }
}

// ---- state: frame number, uptime, fps over the last second, hour of day
static void cmd_state(int argc, char **argv, ControlReply *r) {
    (void)argc; (void)argv;
    control_field_int(r, "frame", (int64_t)renderer_present_count());
    control_field_num(r, "uptime_s", boot_ns ? (double)(plat_now_ns() - boot_ns) / 1e9 : 0);
    if (fps_now >= 0) control_field_num(r, "fps", fps_now);
    bool held = false;
    float hour = test_warp_hour(&held);
    if (hour >= 0) control_field_num(r, "hour", hour);  // (left out before the world exists)
    control_field_bool(r, "hour_held", held);
}

// ---- hour [<0-24> | free]: with no argument just the hour
static void cmd_hour(int argc, char **argv, ControlReply *r) {
    if (argc > 2) { control_fail(r, "hour: expected one argument (a number from 0 to 24, or \"free\")"); return; }
    if (argc == 2) {
        if (!strcmp(argv[1], "free")) {
            test_warp_set_hour(-1);
        } else {
            char *end;
            errno = 0;
            double h = strtod(argv[1], &end);
            if (end == argv[1] || *end || errno || !(h >= 0 && h <= 24)) {  // (also rejects nan)
                control_fail(r, "hour: expected a number from 0 to 24 or \"free\", got \"%s\"", argv[1]);
                return;
            }
            test_warp_set_hour((float)h);
        }
    }
    bool held = false;
    float hour = test_warp_hour(&held);
    if (hour >= 0) control_field_num(r, "hour", hour);
    control_field_bool(r, "hour_held", held);
}

// ---- pos: Link's position and stamina (uking::act::PlayerInfo +0x88 / +0xa4, as the clock probe in framepace.c reads them)
static void cmd_pos(int argc, char **argv, ControlReply *r) {
    (void)argv;
    if (argc != 1) { control_fail(r, "pos: takes no arguments"); return; }
    uint64_t pinfo = g_site.player_info ? *(uint64_t *)(uintptr_t)g_site.player_info : 0;
    if (!pinfo) { control_fail(r, "pos: Link does not exist yet (the title screen or a load); load a save first"); return; }
    control_field_num(r, "x", *(float *)(uintptr_t)(pinfo + 0x88));
    control_field_num(r, "y", *(float *)(uintptr_t)(pinfo + 0x8c));
    control_field_num(r, "z", *(float *)(uintptr_t)(pinfo + 0x90));
    control_field_num(r, "stamina", *(float *)(uintptr_t)(pinfo + 0xa4));
    char a[32]; snprintf(a, sizeof a, "0x%llx", (unsigned long long)pinfo);
    control_field_str(r, "player_info", a);  // (the object, for finding pointer paths with mem read)
}

// ---- shot <file>: a PNG of the frame being presented (written before the reply)
static void cmd_shot(int argc, char **argv, ControlReply *r) {
    if (argc != 2) { control_fail(r, "shot: expected one argument, the file to write (use quotes around spaces)"); return; }
    remove(argv[1]);  // (so a failed save can't pass for a new file)
    if (!control_shot_now(argv[1])) { control_fail(r, "shot: no image to save here (not supported by this backend, or nothing presented yet)"); return; }
    struct stat st;
    if (stat(argv[1], &st) || st.st_size <= 0) { control_fail(r, "shot: \"%s\" was not written (does its folder exist?)", argv[1]); return; }
    control_field_str(r, "path", argv[1]);
    control_field_int(r, "bytes", (int64_t)st.st_size);
    control_field_int(r, "frame", (int64_t)renderer_present_count());
}

void control_settings_init(void);  // runtime/control_settings.c
void control_prof_init(void);      // runtime/control_prof.c
void control_input_init(void);     // runtime/control_input.c
void control_mem_init(void);       // runtime/control_mem.c
void control_tp_init(void);        // runtime/control_tp.c

void control_game_init(void) {
    static const ControlCmd cmds[] = {
        {"state", "", "frame number, uptime, fps over the last second, hour of day", cmd_state},
        {"hour", "[<0-24> | free]", "show the hour of the day; with a number hold it there, \"free\" lets it run again", cmd_hour},
        {"pos", "", "Link's position (x y z) and stamina", cmd_pos},
        {"shot", "<file>", "write a PNG of the frame being presented", cmd_shot},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
    control_settings_init();
    control_prof_init();
    control_input_init();
    control_mem_init();
    control_tp_init();
}
