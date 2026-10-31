// Hover activation of the menu cursor (rt_pc_cursor_manager): activating a node plays the game's select sound, so it
// must happen once per change of the highlight. Some screens (the Adventure Log quest list) own their selection and
// put the focus back on it every frame; resting the pointer on a quest used to activate it again and again.
// Host state only: a fake screen, node and cursor control in guest memory, no game code, assets or window.
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../runtime/hle.h"
#undef RT_ON_RESET
#define RT_ON_RESET(fn) static void fn(void)
#include "../runtime/pc_cursor.c"

Settings g_settings = {.pc_cursor = true};
GameSites g_site;
static uint64_t now = 1000000000, frame = 1;
bool menu_cursor_free(void) { return false; }
uint64_t plat_now_ns(void) { return now; }
uint64_t renderer_present_count(void) { return frame; }
bool framepace_menu_ui(void) { return true; }
bool framepace_title_ui(void) { return false; }
uint64_t framepace_script_frame(uint64_t value) { return value; }
void input_pc_escape(bool down) { (void)down; abort(); }
void input_pc_map_axes(float out[2]) { out[0] = out[1] = 0; }
uint64_t input_buttons_now(void) { return 0; }
int rt_opt_pointer_kind(uint64_t row) { (void)row; return 0; }
static Cpu test_cpu;
Cpu *kernel_current_cpu(void) { return &test_cpu; }

enum { FN_ACTIVATE = 0x2000, FN_BOUNDS = 0x2100, FN_NODE_OK = 0x2200, FN_SCREEN_BOX = 0x2300 };
static unsigned activations;
void guest_call(Cpu *cpu, uint64_t address) {
    if (address == FN_ACTIVATE) { activations++; ST64(cpu->x[0] + 0x48, cpu->x[1]); }
    else if (address == FN_BOUNDS) { float *box = (float *)(uintptr_t)cpu->x[0]; box[0] = -100; box[1] = -50; box[2] = 100; box[3] = 50; }
    else if (address == FN_NODE_OK) cpu->x[0] = 1;
    else if (address == FN_SCREEN_BOX) {}
    else abort();
}

static uint64_t mgr, control;
static void step(void) {  // one UI frame of the cursor manager hook
    frame++;
    uint32_t xy[2]; memcpy(xy, point, 8); position = (uint64_t)xy[0] | (uint64_t)xy[1] << 32;
    motion_at = now;
    test_cpu.x[0] = mgr; test_cpu.x[1] = 0;
    rt_pc_cursor_manager(&test_cpu, FN_ACTIVATE, FN_BOUNDS);
}

int main(void) {
    void *memory;
#ifdef _WIN32
    memory = VirtualAlloc((void *)GUEST_LO, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    memory = mmap((void *)GUEST_LO, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
#endif
    assert(memory == (void *)GUEST_LO);
    uint64_t site = GUEST_LO + 0x100, screen_mgr = GUEST_LO + 0x200, screens = GUEST_LO + 0x300;
    uint64_t screen = GUEST_LO + 0x1000, screen_vt = GUEST_LO + 0x2000, node_vt = GUEST_LO + 0x2400;
    uint64_t node = GUEST_LO + 0x3000, button = GUEST_LO + 0x3400, pane = GUEST_LO + 0x3800;
    uint64_t other = GUEST_LO + 0x3c00, kb_node = GUEST_LO + 0x4000;  // the game's own selection / a keyboard target
    mgr = GUEST_LO + 0x5000; control = GUEST_LO + 0x5300;
    g_site.screen_mgr = site; ST64(site, screen_mgr);
    ST64(screen_mgr + 0x30, screens); ST32(screen_mgr + 0x28, 1); ST64(screens, screen);
    ST8(screen + 0xfe, 2); ST32(screen + 0x9c, 0x40); ST64(screen, screen_vt); ST64(screen_vt + 0x38, FN_SCREEN_BOX);
    ST64(screen + 0x88 + 8, node + 0x40); ST64(node + 0x48, screen + 0x88);
    ST64(node, node_vt); ST64(node_vt + 0x28, FN_NODE_OK); ST64(node + 0x18, button); ST64(button + 0x60, pane);
    ST64(mgr + 0x18, control);
    active = true; focused = true; position_inside = true;
    point[0] = 0; point[1] = 0;

    // Resting on a node whose screen puts the focus back on its own selection: one activation, not one per frame.
    ST64(control + 0x48, other);
    step(); assert(activations == 1 && LD64(control + 0x48) == node);
    for (unsigned i = 0; i < 20; i++) { ST64(control + 0x48, other); step(); }
    assert(activations == 1);

    // The game moves its focus elsewhere (keyboard or controller): the pointer takes it back, once.
    ST64(control + 0x48, kb_node); step(); assert(activations == 2);
    for (unsigned i = 0; i < 5; i++) { ST64(control + 0x48, kb_node); step(); }
    assert(activations == 2);

    // Leaving the node and coming back is a new highlight, even with the same focus underneath.
    point[0] = 500; ST64(control + 0x48, kb_node); step(); assert(activations == 2);
    point[0] = 0; ST64(control + 0x48, kb_node); step(); assert(activations == 3);

    // A screen that keeps the activated focus is not activated again either, and navigation away is still taken back.
    step(); step(); assert(activations == 3 && LD64(control + 0x48) == node);
    ST64(control + 0x48, other); step(); assert(activations == 4);

    // A click always has the hovered node focused before the confirm, even when the screen put the focus back.
    ST64(control + 0x48, other); step();  // (restored, nothing to do)
    unsigned before = activations;
    ST64(control + 0x48, other); now += 1; click_deadline = now + 100000000; click_pending = true;
    frame++; sampled_frame = frame;  // (the click was recorded for this frame already)
    uint32_t xy[2]; memcpy(xy, point, 8); position = (uint64_t)xy[0] | (uint64_t)xy[1] << 32; motion_at = now;
    test_cpu.x[0] = mgr; test_cpu.x[1] = 0; ST32(mgr + 0x10, 7);
    rt_pc_cursor_manager(&test_cpu, FN_ACTIVATE, FN_BOUNDS);
    assert(activations == before + 1 && LD64(control + 0x48) == node && LD32(mgr + 0x10) == 1 && !click_pending);
    rt_pc_cursor_manager(&test_cpu, FN_ACTIVATE, FN_BOUNDS);  // the confirm pulse is restored on the next call
    assert(LD32(mgr + 0x10) == 7);

    // A reboot or closed menu forgets the last activation.
    pc_cursor_reset(); active = true; focused = true; position_inside = true;
    ST64(control + 0x48, other); step(); assert(activations == before + 2);

    puts("PASS: hover activates (and plays its select sound) once per highlight change, not every frame while the game restores its own focus; clicks, keyboard focus changes and re-entering a node still activate.");
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, 65536);
#endif
    return 0;
}
