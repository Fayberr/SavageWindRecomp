// Guest memory reads for other control commands (cells, camera, position).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool control_mem_peek(uint64_t addr, void *buf, size_t n);  // read n bytes (committed guest memory only)
