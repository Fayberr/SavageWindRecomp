// Guarded reads of guest memory: they tolerate pages that go away while they are read (unload races).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "memscan.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool ms_guarded(const void *p, size_t n, MsFn fn, void *ctx, size_t *ret) {
    // Clang's x64 SEH scope encloses the call instruction, so callee faults unwind into this handler too.
    // The runtime's vectored lazy-commit handler runs first: callers must exclude uncommitted guest pages.
    __try {
        *ret = fn(p, n, ctx);
    } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                 GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR) ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
    return true;
}

#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/uio.h>
#endif

bool ms_guarded(const void *p, size_t n, MsFn fn, void *ctx, size_t *ret) {
    // Unix callers pass at most 1 MB. One snapshot preserves fn's length and byte-offset semantics.
    // Each worker owns its buffer; the callback must not recursively call ms_guarded on this thread.
    static _Thread_local union { max_align_t align; unsigned char bytes[1024 * 1024]; } copy;
    if (n > sizeof copy.bytes) return false;
    if (n) {
#ifdef __APPLE__
        mach_vm_size_t got = 0;
        if (mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)p, n,
                                  (mach_vm_address_t)(uintptr_t)copy.bytes, &got) != KERN_SUCCESS || got != n)
            return false;
#else
        struct iovec local = {copy.bytes, n}, remote = {(void *)p, n};
        if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != (ssize_t)n) return false;
#endif
    }
    *ret = fn(copy.bytes, n, ctx);
    return true;
}
#endif
