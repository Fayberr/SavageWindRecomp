// Exercise the window-thread cursor policy without opening a window.
// Unrelated guest imports are left out; SDL window operations are replaced below.
#include <assert.h>
#include <stdio.h>
#include "../runtime/hle.h"
// The test calls the host entry points directly; don't root unrelated guest imports in HLE sections.
#undef HLE_FUNC
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#undef RT_ON_RESET
#define RT_ON_RESET(fn) static void fn(void)
#include "../runtime/hle/input.c"
#include "../runtime/hle/bindings.c"

Settings g_settings = {.mouse_camera = true, .pc_cursor = true};
static bool pointer_ui, map_pointer, captured, freed, visible = true, relative;
static int relative_changes, visibility_changes;

bool pc_cursor_active(void) { return pointer_ui; }
bool pc_cursor_hide_system(void) { return map_pointer; }
void pc_cursor_window(SDL_Window *w) { (void)w; }
bool menu_input_captured(void) { return captured; }
bool menu_cursor_free(void) { return freed; }
uint64_t plat_now_ns(void) { return 1000000000; }
void vibration_update(SDL_Gamepad *p, bool focused) { (void)p; (void)focused; }
void motion_tilt_held(bool held) { (void)held; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window *w, bool on) {
    (void)w; relative = on; relative_changes++; return true;
}
bool SDL_CursorVisible(void) { return visible; }
bool SDL_ShowCursor(void) { visible = true; visibility_changes++; return true; }
bool SDL_HideCursor(void) { visible = false; visibility_changes++; return true; }
static bool window_focused = true;
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w) { (void)w; return window_focused ? SDL_WINDOW_INPUT_FOCUS : 0; }
SDL_MouseButtonFlags SDL_GetMouseState(float *x, float *y) { (void)x; (void)y; return 0; }

// Unrelated game/window entry points must never run in this synthetic test.
void pc_cursor_frame(void) { abort(); }
bool framepace_menu_ui(void) { abort(); }
bool framepace_title_ui(void) { abort(); }
Cpu *kernel_current_cpu(void) { abort(); }
bool pc_cursor_event(const SDL_Event *e) { (void)e; return false; }
void motion_pad_opened(SDL_Gamepad *p) { (void)p; abort(); }
void motion_pad_closed(void) { abort(); }
void motion_sensor_event(const SDL_Event *e) { (void)e; abort(); }
void motion_mouse(float x, float y) { (void)x; (void)y; abort(); }
void motion_script(float p, float r, float x, float y) { (void)p; (void)r; (void)x; (void)y; abort(); }
void watch_arm(void) { abort(); }

static void update(void) { input_mouse_update((SDL_Window *)(uintptr_t)1); }
int main(void) {
    unsetenv("SAVAGEWIND_INPUT"); unsetenv("SAVAGEWIND_HOST_INPUT");
    assert(g_bind_key[BIND_RIGHT] == SDL_SCANCODE_1);
    assert(g_bind_key[BIND_LEFT] == SDL_SCANCODE_2);
    assert(g_bind_key[BIND_UP] == SDL_SCANCODE_3);
    assert(g_bind_key[BIND_DOWN] == SDL_SCANCODE_4);
    update(); assert(relative && !visible);
    pointer_ui = true;
    update(); assert(!relative && visible);  // ordinary menus still need the host cursor
    int changes = relative_changes;
    map_pointer = true;
    update(); assert(!relative && !visible && relative_changes == changes);
    // This used to return early because relative mode was already off.
    map_pointer = false;
    update(); assert(visible && relative_changes == changes);
    map_pointer = true; update(); assert(!visible);
    freed = true; update(); assert(visible && !relative);
    freed = false; update(); assert(!visible);
    captured = true; update(); assert(visible);
    captured = false; window_focused = false; update(); assert(visible);
    window_focused = true; update(); assert(!visible);
    pointer_ui = map_pointer = false; update(); assert(relative && !visible);
    g_settings.mouse_camera = false; update(); assert(!relative && visible);
    changes = visibility_changes; update(); assert(visibility_changes == changes);
    puts("PASS: camera/menu/map transitions, absolute map input, F2, focus, overlay and idle cursor visibility");

    // Right click in play stays the player's binding: shield / lock on (ZL), never a bow draw. F stays the bow (ZR).
    assert(g_bind_key[BIND_ZL] == BIND_MOUSE + SDL_BUTTON_RIGHT && g_bind_key[BIND_ZR] == SDL_SCANCODE_F);
    g_settings.mouse_camera = true; pointer_ui = map_pointer = false; update(); assert(relative);
    SDL_Event ev = {0};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN; ev.button.button = SDL_BUTTON_RIGHT; input_handle_event(&ev);
    assert(key_buttons == (1ull << BTN_ZL));
    ev.button.button = SDL_BUTTON_LEFT; input_handle_event(&ev);  // fire while "aiming" is not special any more
    assert(key_buttons == ((1ull << BTN_ZL) | (1ull << BTN_Y)));
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP; input_handle_event(&ev);
    ev.button.button = SDL_BUTTON_RIGHT; input_handle_event(&ev);
    assert(key_buttons == 0);
    ev.type = SDL_EVENT_KEY_DOWN; ev.key.scancode = SDL_SCANCODE_F; input_handle_event(&ev);
    assert(key_buttons == (1ull << BTN_ZR));
    ev.type = SDL_EVENT_KEY_UP; input_handle_event(&ev);
    assert(key_buttons == 0);
    puts("PASS: right click is still shield / lock on (ZL), F still aims the bow (ZR)");
}
