// (test and development only) teleport for the control endpoint (docs/CONTROL.md).
//
// pos reads a copy of Link's position (uking::act::PlayerInfo +0x88) that the game rewrites every frame, so writing
// to it does nothing. The real position is a vec3 somewhere on the game's heap. The first teleport finds it without
// ever blocking a frame and without a visible move:
//   1. a background scan lists every place holding three floats equal to Link's position (about 0.3 s);
//   2. the list is cut to the real one in a few rounds: split it into four groups, nudge the members of group g by
//      g * 0.05 (a few centimetres, undone right after) and read how far pos moved. Copies the game rewrites do not
//      move it, the real one does, so the amount names its group. Four rounds narrow 256 candidates to one;
//   3. the target is written to it and checked.
// That address is reused (re-checked before each use, found again when the game moved it). Better still, path_addr() reaches
// it straight from PlayerInfo (a pointer chain found with mem path), so even the first teleport needs no search.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "control_mem.h"
#include "rt.h"
#include "game_sites.h"

uint64_t renderer_present_count(void);  // presented frames (the backend)

#define MAX_CAND CONTROL_MEM_VEC_MAX
#define NUDGE 0.05f
#define GROUPS 4
#define WAIT_FRAMES 3
#define WAIT_NS 120000000ull  // (the game updates slower than it presents; give it a whole update to overwrite copies)

enum { IDLE, SCAN, PROBE, TEST, VERIFY, DONE, FAILED };
static const char *state_names[] = {"idle", "scanning", "probing", "testing", "verifying", "done", "failed"};
static int state = IDLE;
static float target[3], ttol = 2.0f;      // ttol: how close counts as "at the target" (smaller for a short hop)
static uint64_t cache;                    // the real position's address (0: not found yet)
static uint64_t cand[MAX_CAND];
static float orig[MAX_CAND][3];           // what each candidate held before we touched it
static size_t ncand, tried;
static float p0x;                         // pos.x when a round wrote its nudges
static uint64_t wrote_frame, wrote_ns;
static char why[200];

static float *pinfo_pos(void) {
    uint64_t pinfo = g_site.player_info ? *(uint64_t *)(uintptr_t)g_site.player_info : 0;
    return pinfo ? (float *)(uintptr_t)(pinfo + 0x88) : NULL;
}
// Where the real position lives, as a chain of offsets from PlayerInfo found with mem path (heap addresses move between
// runs, object layouts do not): read the pointer at PlayerInfo+o1, then at that+o2, ..., add the last offset to the final one.
// Checked against pos before every use; if it ever stops matching (another game version) the memory search takes over.
#define PATH_LEN 5
static const uint64_t POS_PATHS[][PATH_LEN] = {
    {0x60, 0x578, 0x80, 0x410, 0x1c0},  // (the same on both boots tried; the ones below hit an array whose slot moved by 8)
    {0x10, 0xe8, 0x750, 0x78, 0x1c0},
    {0x10, 0xe8, 0x758, 0x78, 0x1c0},
};
static uint64_t path_addr(void) {
    uint64_t pinfo = g_site.player_info ? *(uint64_t *)(uintptr_t)g_site.player_info : 0;
    const float *pp = pinfo_pos();
    if (!pinfo || !pp) return 0;
    for (size_t k = 0; k < sizeof POS_PATHS / sizeof *POS_PATHS; k++) {
        uint64_t p = pinfo;
        size_t i = 0;
        for (; i + 1 < PATH_LEN; i++)
            if (!control_mem_peek(p + POS_PATHS[k][i], &p, sizeof p) || p < 0x10000) break;
        if (i + 1 < PATH_LEN) continue;
        p += POS_PATHS[k][PATH_LEN - 1];
        float v[3];
        if (control_mem_peek(p, v, sizeof v) && fabsf(v[0] - pp[0]) <= 1.0f && fabsf(v[2] - pp[2]) <= 1.0f) return p;
    }
    return 0;
}
static bool near_pos(const float *a, const float *b, float tol) {
    return fabsf(a[0] - b[0]) <= tol && fabsf(a[2] - b[2]) <= tol;  // (not y: Link falls after a teleport)
}
static void finish(int st, const char *msg) {
    state = st;
    snprintf(why, sizeof why, "%s", msg);
}
static bool waited(uint64_t frame) { return frame >= wrote_frame + WAIT_FRAMES && plat_now_ns() - wrote_ns >= WAIT_NS; }
static void stamp(uint64_t frame) { wrote_frame = frame; wrote_ns = plat_now_ns(); }

static void restore_round(void) {  // undo this round's nudges (each member that still holds what we wrote)
    size_t sz = (ncand + GROUPS - 1) / GROUPS;
    for (size_t i = sz; i < ncand; i++) {
        float v[3], n = (float)(i / sz) * NUDGE;
        if (control_mem_peek(cand[i], v, sizeof v) && fabsf(v[0] - (orig[i][0] + n)) < NUDGE / 2) control_mem_poke(cand[i], orig[i], sizeof orig[i]);
    }
}

// One round: group 0 is the control, group g gets x + g * NUDGE.
static void start_round(uint64_t frame, bool first) {
    const float *pp = pinfo_pos();
    if (!pp) { finish(FAILED, "Link went away (a load?)"); return; }
    if (ncand == 0) { finish(FAILED, "no copy of Link's position found; he may be loading or in a cutscene"); return; }
    if (ncand == 1) {  // the last one: write the target and see
        if (!control_mem_peek(cand[0], orig[0], sizeof orig[0]) || !control_mem_poke(cand[0], target, sizeof target)) { finish(FAILED, "the position address could not be written"); return; }
        stamp(frame); state = TEST; tried++;
        return;
    }
    size_t sz = (ncand + GROUPS - 1) / GROUPS;
    if (first) p0x = pp[0];  // (later rounds keep it: the game's copy has not yet shown the undone nudge, so reading it now would be off)
    for (size_t i = 0; i < ncand; i++) {
        float v[3];
        if (!control_mem_peek(cand[i], v, sizeof v)) { memset(orig[i], 0, sizeof orig[i]); continue; }
        memcpy(orig[i], v, sizeof v);
        if (i >= sz) { float n3[3] = {v[0] + (float)(i / sz) * NUDGE, v[1], v[2]}; control_mem_poke(cand[i], n3, sizeof n3); }
    }
    tried++; stamp(frame); state = PROBE;
}

void control_tp_frame(uint64_t frame) {
    const float *pp = pinfo_pos();
    if (state == SCAN) {
        size_t n = 0;
        int st = control_mem_find_vec3_poll(cand, MAX_CAND, &n);
        if (st == 1) return;
        if (st == 0) { finish(FAILED, "the position search did not start"); return; }
        if (!pp) { finish(FAILED, "Link went away (a load?)"); return; }
        size_t keep = 0;
        for (size_t i = 0; i < n; i++) if ((uintptr_t)cand[i] != (uintptr_t)pp) cand[keep++] = cand[i];  // (the game's own copy)
        ncand = keep;
        start_round(frame, true);
        return;
    }
    if (state != PROBE && state != TEST && state != VERIFY) return;
    if (!waited(frame)) return;
    if (!pp) { finish(FAILED, "Link went away (a load?)"); return; }
    if (state == PROBE) {
        float d = pp[0] - p0x;
        long g = lroundf(d / NUDGE);
        bool clean = fabsf(d - (float)g * NUDGE) < NUDGE / 2.5f;
        size_t sz = (ncand + GROUPS - 1) / GROUPS;
        restore_round();
        if (!clean || g < 0 || g >= GROUPS || (size_t)g * sz >= ncand) {
            char m[200]; snprintf(m, sizeof m, "his position changed while searching (stand still, no stick, not riding) and the search was unclear (moved %.4f with %zu candidates); try again", d, ncand);
            finish(FAILED, m); return;
        }
        size_t from = (size_t)g * sz, to = from + sz < ncand ? from + sz : ncand;
        memmove(cand, cand + from, (to - from) * sizeof *cand);
        ncand = to - from;
        start_round(frame, false);
        return;
    }
    if (near_pos(pp, target, ttol)) {
        if (state == TEST) cache = cand[0];
        finish(DONE, "Link is at the target");
        return;
    }
    // the address is not the real position (TEST), or went stale (VERIFY): put it back
    float v[3];
    uint64_t a = state == TEST ? cand[0] : cache;
    if (control_mem_peek(a, v, sizeof v) && !memcmp(v, target, sizeof v)) control_mem_poke(a, orig[0], sizeof orig[0]);
    if (state == TEST) { finish(FAILED, "the address found did not hold his position; try again standing still"); return; }
    cache = 0;
    if (!control_mem_find_vec3_start(pp, 0.001f)) { finish(FAILED, "the position search did not start"); return; }
    state = SCAN; why[0] = 0;
}

static bool number(const char *s, float *v) {
    char *end;
    double d = strtod(s, &end);
    if (end == s || *end || !isfinite(d) || fabs(d) > 1e7) return false;
    *v = (float)d;
    return true;
}

static void status_fields(ControlReply *r) {
    control_field_str(r, "state", state_names[state]);
    if (why[0]) control_field_str(r, "message", why);
    if (cache) { char s[32]; snprintf(s, sizeof s, "0x%llx", (unsigned long long)cache); control_field_str(r, "address", s); }
    if (state == PROBE || state == TEST) { control_field_int(r, "candidates", (int64_t)ncand); control_field_int(r, "rounds", (int64_t)tried); }
    const float *pp = pinfo_pos();
    if (pp) { control_field_num(r, "x", pp[0]); control_field_num(r, "y", pp[1]); control_field_num(r, "z", pp[2]); }
}

// teleport <x> <y> <z> | teleport status | teleport forget
static void cmd_teleport(int argc, char **argv, ControlReply *r) {
    uint64_t frame = renderer_present_count();
    if (argc == 2 && !strcmp(argv[1], "status")) { status_fields(r); return; }
    if (argc == 2 && !strcmp(argv[1], "forget")) { cache = 0; state = IDLE; why[0] = 0; control_field_str(r, "state", "idle"); return; }
    float t[3];
    if (argc != 4 || !number(argv[1], &t[0]) || !number(argv[2], &t[1]) || !number(argv[3], &t[2])) {
        control_fail(r, "teleport: use teleport <x> <y> <z> (numbers; start from pos), teleport status, or teleport forget");
        return;
    }
    const float *pp = pinfo_pos();
    if (!pp) { control_fail(r, "teleport: Link does not exist yet (the title screen or a load); load a save first"); return; }
    if (state == SCAN || state == PROBE || state == TEST || state == VERIFY) { control_fail(r, "teleport: one is already running; poll teleport status"); return; }
    memcpy(target, t, sizeof target);
    {
        float d = hypotf(t[0] - pp[0], t[2] - pp[2]);
        if (d < 0.05f) { control_fail(r, "teleport: that is where he already is (move at least 0.05 horizontally)"); return; }
        ttol = fmaxf(0.01f, fminf(2.0f, 0.25f * d));  // (a copy that did not move is d away; the real one is at the target)
    }
    why[0] = 0; tried = 0;
    float at[3];
    uint64_t pa = path_addr();
    if (pa) cache = pa;  // (the direct route: no search)
    if (cache && control_mem_peek(cache, at, sizeof at) && near_pos(at, pp, 1.0f)) {
        memcpy(orig[0], at, sizeof at);
        if (control_mem_poke(cache, target, sizeof target)) { stamp(frame); state = VERIFY; status_fields(r); return; }
    }
    cache = 0;
    if (!control_mem_find_vec3_start(pp, 0.001f)) { control_fail(r, "teleport: the position search is busy; try again in a second"); return; }
    state = SCAN;
    status_fields(r);
}

void control_tp_init(void) {
    static const ControlCmd cmds[] = {
        {"teleport", "<x> <y> <z> | status | forget",
         "move Link (the first time it searches for his real position in the background; poll teleport status)", cmd_teleport},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
}
