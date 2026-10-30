// The profiler's sampler: while a capture runs, every interval (default 1000 us, SAVAGEWIND_PROF_SAMPLE=<us>, 0 = off)
// it takes the host call stack of every guest thread and writes it into the capture (PROF_SAMPLE + PROF_STACK records,
// runtime/prof.h). tools/swprof.py splits the samples into game code, the SWR runtime and the driver / system, and
// names the runtime's hot functions. The recompiled game code is one total on purpose: nothing here names its functions.
//
// Windows: the thread is suspended, its context read and the stack walked with the x64 unwind data (the game library
//   and the runtime both have it); nothing allocates or locks while a thread is suspended.
// Linux: a signal (SIGPROF) runs in the thread, which unwinds its own stack with the unwind tables (.eh_frame) and hands
//   the addresses to this thread. One thread at a time, so there is one slot.
// Elsewhere: no samples (the zones and counters of the capture are all there).
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "prof.h"
#include "rt.h"

#define MAX_THREADS 128
#define DEPTH 18  // frames kept per sample (6 records): the top ones decide where the time is; the callers name the cause

static pthread_t g_thread;
static int g_running;
static uint32_t g_interval_us;

// One sample: the head record (time, os thread id, kernel handle, depth), then the addresses three to a record.
static void emit(uint64_t t, uint64_t tid, uint32_t handle, const uint64_t *pc, int n) {
    prof_put(PROF_SAMPLE | ((uint32_t)n << 8), 0, t, tid, handle);
    for (int i = 0; i < n; i += 3)
        prof_put(PROF_STACK, 0, pc[i], i + 1 < n ? pc[i + 1] : 0, i + 2 < n ? pc[i + 2] : 0);
}

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

static uint64_t os_tid(HANDLE h) { return GetThreadId(h); }

static void *sampler_main(void *arg) {
    (void)arg;
    prof_thread_name("sampler");
    pthread_t host[MAX_THREADS];
    uint32_t handle[MAX_THREADS];
    uint64_t entry[MAX_THREADS];
    for (uint64_t next = plat_now_ns(); __atomic_load_n(&g_running, __ATOMIC_ACQUIRE) && prof_enabled();
         next += (uint64_t)g_interval_us * 1000) {
        int n = kernel_running_threads(host, handle, entry, MAX_THREADS);
        for (int i = 0; i < n; i++) {
            HANDLE h = (HANDLE)pthread_gethandle(host[i]);
            if (!h || h == GetCurrentThread()) continue;
            uint64_t pc[DEPTH], tid = os_tid(h), t = 0;
            int d = 0;
            if (SuspendThread(h) == (DWORD)-1) continue;
            t = prof_ticks();
            CONTEXT ctx;
            memset(&ctx, 0, sizeof ctx);
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(h, &ctx)) {
                for (; d < DEPTH && ctx.Rip; d++) {
                    pc[d] = ctx.Rip;
                    DWORD64 base = 0;
                    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx.Rip, &base, NULL);
                    if (!f) {  // a leaf without unwind data: the return address is on top of the stack
                        if (!ctx.Rsp || IsBadReadPtr((void *)ctx.Rsp, 8)) { d++; break; }
                        ctx.Rip = *(DWORD64 *)ctx.Rsp;
                        ctx.Rsp += 8;
                        continue;
                    }
                    void *hd = NULL;
                    DWORD64 fr = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, f, &ctx, &hd, &fr, NULL);
                }
            }
            ResumeThread(h);
            if (d) emit(t, tid, handle[i], pc, d);
        }
        uint64_t now = plat_now_ns();
        if (next > now) plat_sleep_until_ns(next);
        else next = now;
    }
    return NULL;
}

int prof_sampler_modules(ProfModule *out, int max) {
    HMODULE mods[256];
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return 0;
    int n = 0;
    HMODULE exe = GetModuleHandleA(NULL);
    for (DWORD i = 0; i < need / sizeof(HMODULE) && n < max; i++) {
        MODULEINFO mi;
        if (!K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
        ProfModule *m = &out[n++];
        memset(m, 0, sizeof *m);
        m->base = m->bias = (uint64_t)(uintptr_t)mi.lpBaseOfDll;
        m->size = mi.SizeOfImage;
        K32GetModuleFileNameExA(GetCurrentProcess(), mods[i], m->name, sizeof m->name - 1);
        m->kind = mods[i] == exe ? PROF_MODULE_RUNTIME : strstr(m->name, "swgame") ? PROF_MODULE_GAME : PROF_MODULE_OTHER;
    }
    return n;
}

#elif defined(__linux__)
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif

// The slot the signal handler fills: the sampler asks one thread at a time and waits for it.
static struct {
    volatile uint32_t asked, answered;  // request numbers
    pthread_t target;                   // the thread asked
    volatile uint64_t tid;              // its os thread id, from the handler
    uint64_t pc[DEPTH + 4];
    volatile int n;
} g_slot;

static void on_signal(int sig, siginfo_t *si, void *uc_) {
    (void)sig; (void)si;
    uint32_t ask = __atomic_load_n(&g_slot.asked, __ATOMIC_ACQUIRE);
    if (ask == g_slot.answered || !pthread_equal(g_slot.target, pthread_self())) return;  // (late, or not meant for this thread)
    g_slot.tid = (uint64_t)syscall(SYS_gettid);
    ucontext_t *uc = uc_;
    uint64_t pc0 = 0;
#if defined(__x86_64__)
    pc0 = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    pc0 = (uint64_t)uc->uc_mcontext.pc;
#endif
    (void)uc;
    int n = 0;
    uint64_t *out = g_slot.pc;
    if (pc0) out[n++] = pc0;
#ifdef __GLIBC__
    void *bt[DEPTH + 6];
    int m = backtrace(bt, DEPTH + 6);
    // the frames above the interrupted one are this handler and the signal return; start after the frame at the
    // interrupted pc when it is there, else after those two
    int from = 2;
    for (int k = 0; k < m && k < 5; k++)
        if (pc0 && (uint64_t)(uintptr_t)bt[k] == pc0) { from = k + 1; break; }
    for (int k = from; k < m && n < DEPTH; k++) out[n++] = (uint64_t)(uintptr_t)bt[k];
#endif
    g_slot.n = n;
    __atomic_store_n(&g_slot.answered, ask, __ATOMIC_RELEASE);
}

static void *sampler_main(void *arg) {
    (void)arg;
    prof_thread_name("sampler");
#ifdef __GLIBC__
    void *warm[4];
    backtrace(warm, 4);  // (the first call loads the unwinder: not in a signal handler)
#endif
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, &old);
    pthread_t host[MAX_THREADS];
    uint32_t handle[MAX_THREADS];
    uint64_t entry[MAX_THREADS];
    uint32_t req = 0;
    for (uint64_t next = plat_now_ns(); __atomic_load_n(&g_running, __ATOMIC_ACQUIRE) && prof_enabled();
         next += (uint64_t)g_interval_us * 1000) {
        int n = kernel_running_threads(host, handle, entry, MAX_THREADS);
        for (int i = 0; i < n; i++) {
            if (pthread_equal(host[i], pthread_self())) continue;
            g_slot.n = 0;
            g_slot.target = host[i];
            req++;
            uint64_t t = prof_ticks();
            __atomic_store_n(&g_slot.asked, req, __ATOMIC_RELEASE);
            if (pthread_kill(host[i], SIGPROF) != 0) continue;
            for (int spin = 0; spin < 250 && __atomic_load_n(&g_slot.answered, __ATOMIC_ACQUIRE) != req; spin++) {
                struct timespec ts = {0, 20000};
                nanosleep(&ts, NULL);
            }
            if (g_slot.answered == req && g_slot.n > 0) emit(t, g_slot.tid, handle[i], g_slot.pc, g_slot.n);
        }
        uint64_t now = plat_now_ns();
        if (next > now) plat_sleep_until_ns(next);
        else next = now;
    }
    sigaction(SIGPROF, &old, NULL);
    return NULL;
}

static int module_cb(struct dl_phdr_info *info, size_t sz, void *arg) {
    (void)sz;
    struct { ProfModule *out; int n, max; } *a = arg;
    if (a->n >= a->max) return 1;
    uint64_t lo = ~0ull, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++)
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            uint64_t s = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr, e = s + info->dlpi_phdr[i].p_memsz;
            if (s < lo) lo = s;
            if (e > hi) hi = e;
        }
    if (lo >= hi) return 0;
    ProfModule *m = &a->out[a->n];
    memset(m, 0, sizeof *m);
    m->base = lo;
    m->size = hi - lo;
    m->bias = info->dlpi_addr;
    snprintf(m->name, sizeof m->name, "%s", info->dlpi_name[0] ? info->dlpi_name : "(runtime)");
    m->kind = a->n == 0 ? PROF_MODULE_RUNTIME : strstr(m->name, "swgame") ? PROF_MODULE_GAME : PROF_MODULE_OTHER;
    a->n++;
    return 0;
}

int prof_sampler_modules(ProfModule *out, int max) {
    struct { ProfModule *out; int n, max; } a = {out, 0, max};
    dl_iterate_phdr(module_cb, &a);
    return a.n;
}

#else  // no sampler on this platform

static void *sampler_main(void *arg) { (void)arg; return NULL; }
int prof_sampler_modules(ProfModule *out, int max) { (void)out; (void)max; return 0; }

#endif

void prof_sampler_begin(void) {
    const char *e = getenv("SAVAGEWIND_PROF_SAMPLE");
    long us = e ? atol(e) : 1000;
    if (us <= 0 || g_running) return;
    if (us < 100) us = 100;
    g_interval_us = (uint32_t)us;
    __atomic_store_n(&g_running, 1, __ATOMIC_RELEASE);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
}

void prof_sampler_end(void) {
    if (!g_running) return;
    __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
    pthread_join(g_thread, NULL);
}
