// On-demand profiler: the recorder (runtime/prof.c) and its markers. Analysis: tools/swprof.py.
//
// Off (the normal state) a marker costs one load and one branch. A capture is started with prof_start() (the
// `prof` command, the hotkey, SAVAGEWIND_PROF=<first frame>[:<frames>]) and runs until prof_stop(); then one
// file, profile/swprof-<time>.swprof, is written on a thread of its own. The recorder is built so that a running
// capture disturbs the game as little as it can: a cheap cycle counter, one write-only buffer per thread (no locks,
// no allocation, no formatting), everything else is done afterwards by the tool.
//
//   SW_ZONE("name");      the rest of this block, as one event with a start and an end. Zones nest per thread.
//   SW_ACC("a/b");        like a zone, but the time and the calls are added up per thread and frame and written once a
//                         frame: for work done thousands of times a frame (a draw's steps), where an event each would
//                         fill the buffer. "/" in the name nests it under "a" in the report.
//   SW_COUNT("name", n);  a number for this moment (draws, bytes, queue depth)
//   prof_frame(n);        the presenting thread, once per frame: frame n starts
//   prof_thread_name(s);  names the calling thread in the report
//
// Compile every marker away with -DSW_PROF_DISABLE.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- control (any thread; the command layer, the hotkey and the environment variable call these)
bool prof_start(void);             // false: already running, still writing the last file, or no memory
void prof_stop(void);              // ends the capture; the file is written in the background
bool prof_toggle(void);            // start or stop; true = now capturing
bool prof_active(void);
// One line of state for a command: capturing / writing / idle, how full the buffer is, the last file.
void prof_status(char *out, size_t n);
void prof_frame(uint64_t frame);   // the presenting thread, at the start of each frame
void prof_thread_name(const char *name);
// The last file written ("" before the first), for a command to report.
const char *prof_last_file(void);

// ---- markers
typedef struct { const char *name; uint32_t id; } ProfSite;  // id 0: not registered yet
typedef struct { uint64_t t0, t1, val; uint32_t site, kind; } ProfRec;  // 32 bytes. kind: low 8 bits PROF_*, ACC: calls << 8
enum { PROF_ZONE = 1, PROF_COUNTER = 2, PROF_FRAME = 3, PROF_ACC = 4 };

typedef struct ProfAcc ProfAcc;
typedef struct {
    ProfRec *cur, *end;
    ProfAcc *acc;
    uint64_t acc_frame;
    uint32_t gen, acc_seq, thread;
} ProfCursor;

extern volatile int g_prof_on;
extern volatile uint32_t g_prof_gen;  // changes with every capture: a thread's cursor from an older one is stale
extern volatile uint32_t g_prof_seq;  // counts frames: a thread flushes its ACC totals when it sees a new one
extern __thread ProfCursor t_prof;

uint32_t prof_register(ProfSite *s);
bool prof_slow(ProfCursor *c);                 // a stale or full cursor: get this thread a chunk; false = drop the event
void prof_acc_flush(ProfCursor *c);            // write the totals of the frame that ended

static inline uint64_t prof_ticks(void) {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#elif defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
#error "prof_ticks: no cycle counter for this CPU"
#endif
}

static inline int prof_enabled(void) { return __atomic_load_n(&g_prof_on, __ATOMIC_RELAXED); }

static inline void prof_put(uint32_t kind, uint32_t site, uint64_t t0, uint64_t t1, uint64_t val) {
    ProfCursor *c = &t_prof;
    if (__builtin_expect(c->gen != g_prof_gen || c->cur == c->end, 0))
        if (!prof_slow(c)) return;
    ProfRec *r = c->cur++;
    r->t0 = t0;
    r->t1 = t1;
    r->val = val;
    r->site = site;
    __atomic_store_n(&r->kind, kind, __ATOMIC_RELEASE);  // last: the reader takes a record once its kind is set
}

static inline uint32_t prof_site(ProfSite *s) { return s->id ? s->id : prof_register(s); }

typedef struct { ProfSite *site; uint64_t t0; } ProfZone;  // t0 0: the capture was off when the block began

static inline void prof_zone_exit(ProfZone *z) {
    if (z->t0) prof_put(PROF_ZONE, prof_site(z->site), z->t0, prof_ticks(), 0);
}

static inline void prof_acc_add(ProfSite *s, uint64_t ticks);
static inline void prof_acc_exit(ProfZone *z) {
    if (z->t0) prof_acc_add(z->site, prof_ticks() - z->t0);
}

#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b) PROF_CAT2(a, b)

#ifdef SW_PROF_DISABLE
#define SW_ZONE(name) ((void)0)
#define SW_ACC(name) ((void)0)
#define SW_COUNT(name, n) ((void)0)
#else
#define SW_ZONE(name) \
    static ProfSite PROF_CAT(prof_site_, __LINE__) = {name, 0}; \
    __attribute__((cleanup(prof_zone_exit), unused)) ProfZone PROF_CAT(prof_zone_, __LINE__) = { \
        &PROF_CAT(prof_site_, __LINE__), prof_enabled() ? prof_ticks() : 0}
#define SW_ACC(name) \
    static ProfSite PROF_CAT(prof_site_, __LINE__) = {name, 0}; \
    __attribute__((cleanup(prof_acc_exit), unused)) ProfZone PROF_CAT(prof_zone_, __LINE__) = { \
        &PROF_CAT(prof_site_, __LINE__), prof_enabled() ? prof_ticks() : 0}
#define SW_COUNT(name, n) \
    do { \
        static ProfSite prof_cs_ = {name, 0}; \
        if (prof_enabled()) prof_put(PROF_COUNTER, prof_site(&prof_cs_), prof_ticks(), (uint64_t)(n), 0); \
    } while (0)
#endif

// Per-thread, per-frame totals for SW_ACC: indexed by site id.
#define PROF_MAX_SITES 1024
struct ProfAcc {
    uint64_t ticks[PROF_MAX_SITES];
    uint32_t calls[PROF_MAX_SITES];
};

static inline void prof_acc_add(ProfSite *s, uint64_t ticks) {
    ProfCursor *c = &t_prof;
    if (__builtin_expect(c->gen != g_prof_gen || !c->acc, 0))
        if (!prof_slow(c)) return;
    if (__builtin_expect(c->acc_seq != g_prof_seq, 0)) prof_acc_flush(c);
    uint32_t id = prof_site(s);
    c->acc->ticks[id] += ticks;
    c->acc->calls[id]++;
}

#ifdef __cplusplus
}
#endif
