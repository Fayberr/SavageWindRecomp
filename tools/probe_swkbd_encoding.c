// Diagnostic for #271. Calls the actual HLE with synthetic arguments; needs no game, window or renderer.
// clang -std=gnu11 -Wall -Wextra -Werror tools/probe_swkbd_encoding.c -o /tmp/probe_swkbd_encoding
// MISMATCH means the current HLE does not honour the requested encoding. This is an investigation probe.
#include <assert.h>
#include "../runtime/hle/swkbd.c"

static const char *answer;
static char seen_initial[96];

int menu_keyboard_run(const char *initial, int max_len, char *out, int cap) {
    assert(max_len == 9);
    snprintf(seen_initial, sizeof seen_initial, "%s", initial);
    snprintf(out, (size_t)cap, "%s", answer);
    return 1;
}

static void hex(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static int probe(const char *label, const char *text, const unsigned char *utf16, size_t bytes, int utf8) {
    uint64_t arg[0x550 / 8] = {0};
    struct { unsigned char before[8], data[64], after[8]; } buffer;
    memset(&buffer, 0xa5, sizeof buffer);
    uint64_t result[2] = {(uint64_t)(uintptr_t)buffer.data, sizeof buffer.data};
    Cpu cpu = {0};
    cpu.x[0] = (uint64_t)(uintptr_t)result;
    cpu.x[1] = (uint64_t)(uintptr_t)arg;
    ST32(cpu.x[1] + 0x3ac, 9);
    ST8(cpu.x[1] + 0x3bd, utf8);
    answer = text;
    swkbd_show(&cpu);
    assert(cpu.x[0] == 0);
    for (size_t i = 0; i < sizeof buffer.before; i++) {
        assert(buffer.before[i] == 0xa5);
        assert(buffer.after[i] == 0xa5);
    }
    const unsigned char *expected = utf8 ? (const unsigned char *)text : utf16;
    size_t size = utf8 ? strlen(text) + 1 : bytes;
    assert(size <= sizeof buffer.data);
    int match = memcmp(buffer.data, expected, size) == 0;
    printf("%s, requested UTF-%s: %s\n  actual:   ", label, utf8 ? "8" : "16LE", match ? "MATCH" : "MISMATCH");
    hex(buffer.data, size);
    printf("\n  expected: ");
    hex(expected, size);
    printf("\n");
    return match;
}

int main(void) {
    static const struct {
        const char *label, *utf8;
        const unsigned char *utf16;
        size_t bytes;
    } cases[] = {
        {"Talon", "Talon", (const unsigned char *)"T\0a\0l\0o\0n\0\0", 12},
        {"AA", "AA", (const unsigned char *)"A\0A\0\0", 6},
        {"Nintendo", "Nintendo", (const unsigned char *)"N\0i\0n\0t\0e\0n\0d\0o\0\0", 18},
        {"A", "A", (const unsigned char *)"A\0\0", 4},
        {"accent", "\xc3\x89pona", (const unsigned char *)"\xc9\0p\0o\0n\0a\0\0", 12},
        {"CJK", "\xe9\xa9\xac", (const unsigned char *)"\x6c\x9a\0", 4},
        {"surrogate pair", "\xf0\x9f\x90\x8e", (const unsigned char *)"\x3d\xd8\x0e\xdc\0", 6},
    };
    int matches[2] = {0};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
        for (int utf8 = 0; utf8 <= 1; utf8++)
            matches[utf8] += probe(cases[i].label, cases[i].utf8, cases[i].utf16, cases[i].bytes, utf8);
    printf("Encoding matches: UTF-16LE %d/7, UTF-8 %d/7\n", matches[0], matches[1]);

    // The 1.9.0 SDK places the work buffer at +0x518. Its initial text is UTF-16 regardless of output mode.
    uint64_t arg[0x550 / 8] = {0};
    uint16_t work[] = {'T', 'a', 'l', 'o', 'n', 0};
    unsigned char buffer[64];
    uint64_t result[2] = {(uint64_t)(uintptr_t)buffer, sizeof buffer};
    Cpu cpu = {0};
    cpu.x[0] = (uint64_t)(uintptr_t)result;
    cpu.x[1] = (uint64_t)(uintptr_t)arg;
    ST32(cpu.x[1] + 0x3ac, 9);
    ST32(cpu.x[1] + 0x3c4, 5);
    ST64(cpu.x[1] + 0x518, (uint64_t)(uintptr_t)work);
    answer = "Talon";
    swkbd_show(&cpu);
    printf("Initial text at 1.9.0 work-buffer offset: expected Talon, received [%s]\n", seen_initial);
    return 0;
}
