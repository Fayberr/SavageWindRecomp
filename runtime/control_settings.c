// The `get` command: the current settings, read only. (`set` is a developer command.)
#include "control.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { BOOL, INT, FLOAT, TEXT } Kind;
typedef struct {
    const char *key;
    void *value;
    Kind kind;
    double min, max;
    const char *names; // enum names in numeric order, separated by '|'
} Setting;
#define B(k) {#k, &g_settings.k, BOOL, 0, 1, "off|on"}
#define I(k, lo, hi, names) {#k, &g_settings.k, INT, lo, hi, names}
#define F(k, lo, hi) {#k, &g_settings.k, FLOAT, lo, hi, NULL}
static const Setting settings[] = {
    B(fullscreen), I(window_w, 0, 16384, NULL), I(window_h, 0, 16384, NULL),
    I(upscaler, 0, UPSCALE_COUNT - 1, "off|metalfx|fsr1|fsr3|dlss"),
    I(upscale_quality, 0, UPQ_COUNT - 1, "ultra_quality|quality|balanced|performance|ultra_performance"),
    I(antialias, 0, AA_TAA, "off|smaa|taa"),
    I(anisotropy, 0, 4, "game|2x|4x|8x|16x"), I(sharpening, 0, 10, NULL),
    B(frame_gen), F(volume, 0, 2), B(reverb), B(movie_audio),
    I(perf_overlay, 0, PERF_FULL, "off|fps|full"), I(fps_limit, 0, 120, NULL),
    I(vsync, 0, VSYNC_COUNT - 1, "on|fast|adaptive|off"), B(dpad_move),
    {"stick_deadzone[0]", &g_settings.stick_deadzone[0], FLOAT, 0, .5, NULL},
    {"stick_deadzone[1]", &g_settings.stick_deadzone[1], FLOAT, 0, .5, NULL},
    B(mouse_camera), F(mouse_sens, .1, 10), F(mouse_sens_y, .5, 2), B(mouse_invert_y),
    F(motion_sens, .1, 10), I(motion_invert, 0, 3, "off|up_down|left_right|both"),
    I(button_icons, 0, ICONS_COUNT - 1, "auto|keyboard|xbox|playstation|nintendo"),
    B(async_shaders), B(precompile_shaders),
    I(shadow_res, 0, SHADOW_COUNT - 1, "console|high|ultra|extreme"), B(wide_view),
    I(hdr, 0, HDR_COUNT - 1, "auto|on|off"), I(hdr_white, 0, 1000, NULL),
    I(fov, 50, 200, NULL), I(color_exposure, -20, 20, NULL), I(color_contrast, -50, 50, NULL),
    I(color_saturation, -100, 100, NULL), I(color_temperature, -100, 100, NULL),
    I(color_tint, -100, 100, NULL),
    {"color_lut", g_settings.color_lut, TEXT, 0, 0, NULL},
    I(color_lut_strength, 0, 100, NULL), B(color_lut_set),
    I(text_language, 0, 0, NULL), I(voice_language, 0, 0, NULL), B(menu_tip_seen),
};
#undef B
#undef I
#undef F

static const Setting *find(const char *key) {
    if (!strcmp(key, "fps")) key = "fps_limit";
    if (!strcmp(key, "left_stick_deadzone")) key = "stick_deadzone[0]";
    if (!strcmp(key, "right_stick_deadzone")) key = "stick_deadzone[1]";
    for (size_t i = 0; i < sizeof settings / sizeof *settings; i++)
        if (!strcmp(key, settings[i].key)) return &settings[i];
    return NULL;
}
static void emit(const Setting *s, ControlReply *r) {
    if (s->kind == TEXT) { control_field_str(r, s->key, s->value); return; }
    if (s->kind == BOOL) { control_field_bool(r, s->key, *(bool *)s->value); return; }
    if (s->kind == FLOAT) { control_field_num(r, s->key, *(float *)s->value); return; }
    int v = *(int *)s->value;
    const char *name = s->names;
    if (name && v >= 0 && v <= s->max) {
        for (int i = 0; i < v; i++) name = strchr(name, '|') + 1;
        size_t n = strcspn(name, "|");
        char buf[64];
        memcpy(buf, name, n); buf[n] = 0;
        control_field_str(r, s->key, buf);
    } else control_field_int(r, s->key, v);
}
static void window_size_reply(ControlReply *r) {
    char buf[48];
    if (!g_settings.window_w && !g_settings.window_h) strcpy(buf, "default");
    else snprintf(buf, sizeof buf, "%dx%d", g_settings.window_w, g_settings.window_h);
    control_field_str(r, "window_size", buf);
}
static void cmd_get(int argc, char **argv, ControlReply *r) {
    if (argc > 2) { control_fail(r, "Usage: get [key]"); return; }
    if (argc == 2) {
        if (!strcmp(argv[1], "window_size")) { window_size_reply(r); return; }
        const Setting *s = find(argv[1]);
        if (!s) control_fail(r, "Unknown setting: %s", argv[1]);
        else emit(s, r);
    } else {
        for (size_t i = 0; i < sizeof settings / sizeof *settings; i++) emit(&settings[i], r);
        window_size_reply(r);
    }
}
void control_settings_init(void) {
    static const ControlCmd get[] = {
        {"get", "[key]", "List the settings or show one", cmd_get, CONTROL_PLAYER},
    };
    control_register(get, 1);
}
