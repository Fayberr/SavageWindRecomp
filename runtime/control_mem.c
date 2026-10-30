// Reads of guest memory for the control commands that follow pointers (runtime/control_mem.h). A pointer chain can end
// anywhere, so every read goes through a guarded copy: an address outside the guest window or on a page that went away
// answers false instead of crashing the game.
#include "control_mem.h"
#include "memscan.h"
#include "rt.h"

#include <string.h>

typedef struct { void *buf; size_t n; } PeekCtx;
static size_t peek_fn(const void *p, size_t n, void *ctx) { PeekCtx *c = ctx; memcpy(c->buf, p, c->n); return n; }

bool control_mem_peek(uint64_t a, void *buf, size_t n) {
    if (a < GUEST_LO || a >= GUEST_HI || n > GUEST_HI - a) return false;
    PeekCtx c = {buf, n};
    size_t ret;
    return ms_guarded((const void *)(uintptr_t)a, n, peek_fn, &c, &ret);
}
