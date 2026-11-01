// Exercise the actual Options hooks with the real capture engine and synthetic guest rows.
#include <cassert>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../runtime/ui/game_options.mm"

Settings g_settings = {};
GameSites g_site = {};
const char *g_game_version = "1.9.0";
static Cpu test_cpu;
static unsigned focused_kind, focus_count, help_count;
static uint64_t focused_row;
const char *tr(const char *s) { return s; }
extern "C" {
Cpu *kernel_current_cpu(void) { return &test_cpu; }
void settings_save(void) {}
void menu_size_list_open(void) { assert(false); }
void rt_request_reboot(int) { assert(false); }
int language_pending(void) { return 0; }
int input_last_device(void) { return DEVICE_KEYBOARD; }
int input_pad_family(void) { return 1; }
void pc_cursor_options_focus(uint64_t, uint64_t row, unsigned kind) {
    focused_row = row; focused_kind = kind; focus_count++;
}
void guest_call(Cpu *c, uint64_t fn) {
    assert(fn == g_site.opt_two_help);
    help_count++;
    c->x[19] = 0xdead; c->v[0] = 42;
}
}

int main() {
    constexpr uint64_t base = 0x7000000000ull;
#ifdef _WIN32
    void *mem = VirtualAlloc((void *)base, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *mem = mmap((void *)base, 65536, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
#endif
    assert(mem == (void *)base);
    uint64_t row = base + 0x1000, next = base + 0x1100, ordinary = base + 0x1200;
    uint64_t screen = base + 0x2000;
    g_site.opt_table = base; g_site.opt_two_help = base + 0x8000;
    g_installed = 1; wr64(OPT_ENTRIES, g_installed);
    for (int i = 0; i < 3; i++) {
        g_items.push_back(button(P_BINDS, "test", "test", nullptr, A_BIND, BIND_ACTION + i));
        uint64_t r = i == 0 ? row : i == 1 ? next : ordinary;
        wr32(r + 0x50, GAME_ENTRIES + i); wr64(r, screen);
        wr64(r + 0x20, base + 0x6000 + i * 0x200);
        wr64(r + 0x28, base + 0x6100 + i * 0x200);
    }
    g_two_rows = {{row}, {next}}; g_cur_page = P_BINDS;
    act_reset();
    wr64(screen + 0x36c8, row); wr32(screen + 0x36c0, 0);
    assert(rt_opt_decide(row) == 2);
    int dev, slot, ms;
    assert(bind_capture_active(&dev, &slot, &ms) && dev == BIND_DEV_KEY && slot == BIND_ACTION);
    int before = g_act_key[0];
    SDL_Event e = {}; e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; e.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
    bind_capture_event(&e);
    assert(bind_capture_active(&dev, &slot, &ms) && g_act_key[0] == before);
    e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = SDL_SCANCODE_F8;
    bind_capture_event(&e); assert(!bind_capture_active(&dev, &slot, &ms) && g_act_key[0] == SDL_SCANCODE_F8);
    rt_opt_two_horizontal(screen, 2);
    assert(!bind_capture_active(&dev, &slot, &ms) && focused_kind == KIND_BUTTON && focused_row == row);
    assert(rd32(screen + 0x36c0) == 3);
    rt_opt_decide(row); assert(!bind_capture_active(&dev, &slot, &ms)); // idle is not confirmation
    wr32(screen + 0x36c0, 0); rt_opt_decide(row);
    assert(bind_capture_active(&dev, &slot, &ms) && dev == BIND_DEV_PAD);
    before = g_act_pad[0]; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = SDL_SCANCODE_F10;
    bind_capture_event(&e); assert(bind_capture_active(&dev, &slot, &ms) && g_act_pad[0] == before);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; e.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
    bind_capture_event(&e); assert(g_act_pad[0] == SDL_GAMEPAD_BUTTON_NORTH && g_act_key[0] == SDL_SCANCODE_F8);
    // Up/down enters a different row's left control but preserves the right column.
    test_cpu.x[19] = 17; test_cpu.v[0] = 23;
    assert(rt_opt_two_callback(screen, rd64(next + 0x20), 0));
    assert(focused_row == next && focused_kind == KIND_BUTTON && rd64(screen + 0x36c8) == next);
    assert(test_cpu.x[19] == 17 && test_cpu.v[0] == 23 && help_count == 1);
    // Explicit mouse hover selects either device even after crossing rows.
    rt_opt_two_pointer(row, KIND_CHOICE);
    assert(rt_opt_two_callback(screen, rd64(row + 0x20), 0) && rt_opt_two_column(row) == 0);
    assert(!rt_opt_two_callback(screen, rd64(ordinary + 0x28), 0));
    assert(rt_opt_two_column(ordinary) == -1 && !rt_opt_two_control(ordinary));
    wr32(screen + 0x36c0, 0); rt_opt_decide(ordinary);
    assert(bind_capture_active(&dev, &slot, &ms) && dev == BIND_DEV_ANY);
    bind_capture_cancel();
    puts("PASS: real Options hooks and capture engine, separate devices, navigation, mouse, CPU preservation, fallback");
}
