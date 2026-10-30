// Guarded reads of guest memory for the control endpoint (runtime/control_mem.c): the OS helpers in runtime/memscan_os.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Run fn(p, n, ctx) over memory that may become inaccessible while it runs, without crashing: on Windows under SEH
// (__try), elsewhere by copying through the OS first (fn then sees a copy). Returns false if the access faulted;
// *ret gets fn's result otherwise.
typedef size_t (*MsFn)(const void *p, size_t n, void *ctx);
bool ms_guarded(const void *p, size_t n, MsFn fn, void *ctx, size_t *ret);
