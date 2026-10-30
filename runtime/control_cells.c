// Named game state cells (control_cells.h): for now the orbit camera, which `camera` and `pos` fall back to.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control_cells.h"
#include "control_mem.h"
#include "game_sites.h"

static uint64_t root_object(const ControlCell *c) {
    uint64_t site = *(const uint64_t *)((const char *)&g_site + c->root);
    uint64_t obj = 0;
    return site && control_mem_peek(site, &obj, sizeof obj) ? obj : 0;
}
static size_t elem_size(CellType t) { (void)t; return 4; }  // (all three types are 32 bits)

uint64_t control_cell_addr(const ControlCell *c) {
    uint64_t root = root_object(c);
    if (!root) return 0;
    size_t sz = elem_size(c->type) * (size_t)c->count;
    for (int k = 0; k < c->nchains; k++) {
        const CellChain *ch = &c->chains[k];
        uint64_t p = root;
        int i = 0;
        for (; i + 1 < ch->len; i++)
            if (!control_mem_peek(p + ch->off[i], &p, sizeof p) || p < 0x10000) break;
        if (i + 1 < ch->len) continue;
        p += ch->off[ch->len - 1];
        uint8_t v[16];
        if (!control_mem_peek(p, v, sz)) continue;
        if (!c->check || c->check(v)) return p;
    }
    return 0;
}

bool control_cell_get(const ControlCell *c, double *out) {
    uint64_t a = control_cell_addr(c);
    uint8_t v[16];
    if (!a || !control_mem_peek(a, v, elem_size(c->type) * (size_t)c->count)) return false;
    for (int i = 0; i < c->count; i++) {
        switch (c->type) {
        case CELL_F32: { float f; memcpy(&f, v + 4 * i, 4); out[i] = f; break; }
        case CELL_U32: { uint32_t u; memcpy(&u, v + 4 * i, 4); out[i] = u; break; }
        case CELL_S32: { int32_t s; memcpy(&s, v + 4 * i, 4); out[i] = s; break; }
        }
    }
    return true;
}

bool control_cell_camera_angles(double e[3], double t[3], double *yaw, double *pitch) {
    double p[1];
    if (!control_cell_get(&control_cell_camera_eye, e) || !control_cell_get(&control_cell_camera_target, t) ||
        !control_cell_get(&control_cell_camera_pitch, p)) return false;
    double dx = t[0] - e[0], dy = t[1] - e[1], dz = t[2] - e[2];
    if (dx * dx + dy * dy + dz * dz < 1e-12) return false;
    *yaw = atan2(dx, dz) * 57.29577951308232;
    *pitch = -p[0];
    return true;
}

// ---- the cells

// The orbit camera behind Link (found by rotating candidate eye positions around him and watching the camera, 1.9.0).
// One object holds eye (x y z), target (x y z, a little above Link), up, fov, near and far; the game keeps the eye as
// state, not as a copy: moving it around the target turns the camera, and it stays there. The pitch is a separate float
// (minus the pitch in degrees, positive = looking down) a few objects further on. Yaw has no cell of its own: it is the
// direction of eye -> target. Both chains start at the same two objects (PlayerInfo +0x60 -> +0x2b8, or the twin through
// +0x328) and were found with mem path. A reading is accepted when it is near Link's own position copy.
static bool near_link(const float *v, float within) {
    uint64_t pinfo = g_site.player_info ? *(const uint64_t *)(uintptr_t)g_site.player_info : 0;
    if (!pinfo) return false;
    const float *pp = (const float *)(uintptr_t)(pinfo + 0x88);
    for (int i = 0; i < 3; i++) if (!isfinite(v[i])) return false;
    float dx = v[0] - pp[0], dy = v[1] - pp[1], dz = v[2] - pp[2];
    return dx * dx + dy * dy + dz * dz <= within * within;
}
static bool eye_check(const void *value) { return near_link(value, 300.0f); }
static bool target_check(const void *value) { return near_link(value, 20.0f); }
static bool pitch_check(const void *value) { float v = *(const float *)value; return isfinite(v) && fabsf(v) <= 90.01f; }
static const CellChain eye_chains[] = {{3, {0x60, 0x2b8, 0x5b0}}, {3, {0x60, 0x328, 0x540}}};
static const CellChain target_chains[] = {{3, {0x60, 0x2b8, 0x5bc}}, {3, {0x60, 0x328, 0x54c}}};
static const CellChain pitch_chains[] = {{4, {0x60, 0x2b8, 0x2a8, 0xd14}}, {4, {0x60, 0x328, 0x238, 0xd14}}};
const ControlCell control_cell_camera_eye = {
    "camera.eye", "the camera's position x y z (move it around camera.target to turn the camera)", offsetof(GameSites, player_info), CELL_F32, 3,
    eye_chains, 2, eye_check,
};
const ControlCell control_cell_camera_target = {
    "camera.target", "the point the camera looks at x y z (a little above Link)", offsetof(GameSites, player_info), CELL_F32, 3,
    target_chains, 2, target_check,
};
const ControlCell control_cell_camera_pitch = {
    "camera.pitch", "minus the camera pitch in degrees (positive looks down; the game clamps it to +-89)", offsetof(GameSites, player_info), CELL_F32, 1,
    pitch_chains, 2, pitch_check,
};
