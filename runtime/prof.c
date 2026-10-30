// On-demand profiler: the recorder and the capture file (runtime/prof.h has the markers and the overview).
//
// Memory: one pool of fixed chunks, taken whole by whichever thread needs one (an atomic counter, nothing else is
// shared), so a thread writes into its own chunk and nobody ever waits. The pool is calloc'ed once, on the first
// capture, and stays zero between captures (the used chunks are cleared after the file is written): a record
// counts as written once its `kind` is not zero, and the reader stops at the first zero one. When the pool is
// full the capture ends itself (SAVAGEWIND_PROF_MB, default 256).
//
// File (little endian), read by tools/swprof.py:
//   header   "SWPROF01", u32 version, u32 record size, u64 ticks0, ns0, ticks1, ns1 (the cycle counter and
//            plat_now_ns at start and stop: the tool derives the counter's rate), u64 unix seconds at start,
//            u32 sites, threads, chunks, u32 reserved
//   sites    per site: u32 id, u16 length, name
//   threads  per thread: u32 index, u32 reserved, u64 os thread id, char name[32]
//   modules  (the header's last u32 says how many) per module: u64 base, size, bias, u32 kind, char name[196]
//   chunks   per chunk: u32 thread, u32 records, then the records
#include "prof.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <pthread.h>
#endif

#include "rt.h"

#define CHUNK_RECS 8192  // 256 KB
#define MAX_THREADS 256
#define FILE_VERSION 1

volatile int g_prof_on;
volatile uint32_t g_prof_gen = 1;
volatile uint32_t g_prof_seq;
static volatile uint64_t g_prof_frame;
__thread ProfCursor t_prof;
static __thread char t_name[32];

enum { IDLE, CAPTURING, WRITING };
static atomic_int state;
static atomic_bool stop_requested;  // the pool filled up: the presenting thread ends the capture at the next frame

static pthread_mutex_t site_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *site_names[PROF_MAX_SITES];
static uint32_t nsites;  // ids 1..nsites

static struct { char name[32]; uint64_t tid; } threads[MAX_THREADS];
static atomic_uint nthreads;

static ProfRec *pool;
static uint16_t *chunk_thread;
static uint32_t pool_chunks;
static atomic_uint next_chunk;

static uint64_t ticks0, ns0, ticks1, ns1, unix0;
static char last_file[256];
static uint64_t last_records;

uint32_t prof_register(ProfSite *s) {
    pthread_mutex_lock(&site_lock);
    if (!s->id) {
        if (nsites + 1 < PROF_MAX_SITES) {
            site_names[++nsites] = s->name;
            s->id = nsites;
        } else {
            s->id = PROF_MAX_SITES - 1;  // out of ids: the rest share the last one
            site_names[s->id] = "(too many markers)";
            if (nsites < s->id) nsites = s->id;
        }
    }
    pthread_mutex_unlock(&site_lock);
    return s->id;
}

static uint64_t os_thread_id(void) {
#ifdef _WIN32
    return GetCurrentThreadId();
#elif defined(__linux__)
    return (uint64_t)syscall(SYS_gettid);
#else
    uint64_t t = 0;
    pthread_threadid_np(NULL, &t);
    return t;
#endif
}

void prof_thread_name(const char *name) {
    snprintf(t_name, sizeof t_name, "%s", name ? name : "");
    // already registered in this capture: update the entry
    if (t_prof.gen == g_prof_gen && t_prof.thread < MAX_THREADS)
        snprintf(threads[t_prof.thread].name, sizeof threads[0].name, "%s", t_name);
}

// ---- names made at run time: copied, deduplicated by a hash table (open addressing)
#define DYN_SLOTS 4096
static uint16_t dyn_slot[DYN_SLOTS];  // site id, 0: free
static uint32_t dyn_count;

uint32_t prof_site_dyn(const char *name) {
    uint32_t h = 2166136261u;
    for (const char *p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    uint32_t id = 0;
    pthread_mutex_lock(&site_lock);
    for (uint32_t i = 0; i < DYN_SLOTS; i++) {
        uint16_t e = dyn_slot[(h + i) % DYN_SLOTS];
        if (!e) {
            if (nsites + 1 < PROF_MAX_SITES - 1 && dyn_count < DYN_SLOTS / 2) {
                char *copy = strdup(name);
                if (copy) {
                    site_names[++nsites] = copy;
                    dyn_slot[(h + i) % DYN_SLOTS] = (uint16_t)nsites;
                    dyn_count++;
                    id = nsites;
                }
            }
            break;
        }
        if (!strcmp(site_names[e], name)) { id = e; break; }
    }
    if (!id) {
        id = PROF_MAX_SITES - 1;  // out of ids: the rest share the last one
        site_names[id] = "(too many markers)";
        if (nsites < id) nsites = id;
    }
    pthread_mutex_unlock(&site_lock);
    return id;
}

double prof_ticks_per_ns(void) {
    uint64_t dn = plat_now_ns() - ns0, dt = prof_ticks() - ticks0;
    return dn > 1000000 ? (double)dt / (double)dn : 1.0;  // (less than a millisecond in: no measurement yet)
}

// A cursor with no chunk, or one from an older capture (register this thread again), or a full one (next chunk).
bool prof_slow(ProfCursor *c) {
    if (!prof_enabled()) return false;
    if (c->gen != g_prof_gen) {
        unsigned t = atomic_fetch_add(&nthreads, 1);
        if (t >= MAX_THREADS) {  // more threads than the table holds: this one records nothing
            c->thread = MAX_THREADS;
            c->gen = g_prof_gen;
            return false;
        }
        snprintf(threads[t].name, sizeof threads[t].name, "%s", t_name[0] ? t_name : "thread");
        threads[t].tid = os_thread_id();
        if (!c->acc) c->acc = malloc(sizeof *c->acc);
        if (!c->acc) return false;
        memset(c->acc, 0, sizeof *c->acc);
        c->thread = t;
        c->cur = c->end = NULL;
        c->acc_seq = g_prof_seq;
        c->acc_frame = g_prof_frame;
        c->gen = g_prof_gen;
    }
    if (c->thread >= MAX_THREADS) return false;
    if (c->cur == c->end) {
        unsigned i = atomic_fetch_add(&next_chunk, 1);
        if (i >= pool_chunks) {
            atomic_store(&stop_requested, true);
            __atomic_store_n(&g_prof_on, 0, __ATOMIC_RELAXED);
            return false;
        }
        chunk_thread[i] = (uint16_t)c->thread;
        c->cur = pool + (size_t)i * CHUNK_RECS;
        c->end = c->cur + CHUNK_RECS;
    }
    return true;
}

void prof_acc_flush(ProfCursor *c) {
    uint64_t now = prof_ticks();
    for (uint32_t id = 1; id <= nsites && id < PROF_MAX_SITES; id++)
        if (c->acc->calls[id]) {
            prof_put(PROF_ACC | (c->acc->calls[id] << 8), id, now, c->acc->ticks[id], c->acc_frame);
            c->acc->ticks[id] = 0;
            c->acc->calls[id] = 0;
        }
    c->acc_seq = g_prof_seq;
    c->acc_frame = g_prof_frame;
}

void prof_frame(uint64_t frame) {
    // SAVAGEWIND_PROF=<first frame>[:<frames>]: start at a frame, stop after some (without a count: when asked).
    // SAVAGEWIND_GPUPROF_AT=<frame> is kept as SAVAGEWIND_PROF=<frame>:1.
    static int env_read, env_done;
    static long env_first = -1, env_count;
    static uint64_t env_stop_at;
    if (!env_read) {
        env_read = 1;
        const char *e = getenv("SAVAGEWIND_PROF");
        char alias[32];
        if (!(e && *e) && getenv("SAVAGEWIND_GPUPROF_AT")) {  // the old per-pass GPU timing of one frame
            snprintf(alias, sizeof alias, "%ld:1", strtol(getenv("SAVAGEWIND_GPUPROF_AT"), NULL, 10));
            e = alias;
        }
        if (e && *e) {
            char *end;
            env_first = strtol(e, &end, 10);
            if (*end == ':') env_count = strtol(end + 1, NULL, 10);
            if (env_first < 0) env_first = 0;
        }
    }
    if (env_first >= 0 && !env_done && frame >= (uint64_t)env_first && atomic_load(&state) == IDLE) {
        env_done = 1;
        if (prof_start()) env_stop_at = env_count > 0 ? frame + (uint64_t)env_count : 0;
    }
    if (!prof_enabled()) {
        if (atomic_load(&stop_requested) && atomic_load(&state) == CAPTURING) prof_stop();  // the buffer filled up
        return;
    }
    if (env_stop_at && frame >= env_stop_at) {
        env_stop_at = 0;
        prof_stop();
        return;
    }
    g_prof_frame = frame;
    __atomic_add_fetch(&g_prof_seq, 1, __ATOMIC_RELEASE);
    prof_put(PROF_FRAME, 0, prof_ticks(), 0, frame);
}

bool prof_active(void) { return atomic_load(&state) == CAPTURING; }
const char *prof_last_file(void) { return last_file; }

bool prof_start(void) {
    int idle = IDLE;
    if (!atomic_compare_exchange_strong(&state, &idle, CAPTURING)) return false;
    if (!pool) {
        const char *mb = getenv("SAVAGEWIND_PROF_MB");
        long n = mb ? atol(mb) : 256;
        if (n < 8) n = 8;
        if (n > 4096) n = 4096;
        pool_chunks = (uint32_t)((size_t)n * 1048576 / (CHUNK_RECS * sizeof(ProfRec)));
        pool = calloc(pool_chunks, CHUNK_RECS * sizeof(ProfRec));
        chunk_thread = calloc(pool_chunks, sizeof *chunk_thread);
        if (!pool || !chunk_thread) {
            free(pool);
            free(chunk_thread);
            pool = NULL;
            chunk_thread = NULL;
            LOG("[prof] no memory for the capture buffer (%ld MB)\n", n);
            atomic_store(&state, IDLE);
            return false;
        }
    }
    atomic_store(&next_chunk, 0);
    atomic_store(&nthreads, 0);
    atomic_store(&stop_requested, false);
    unix0 = (uint64_t)time(NULL);
    ns0 = plat_now_ns();
    ticks0 = prof_ticks();
    g_prof_seq = 0;
    g_prof_gen++;  // every thread registers again, with a chunk of its own
    __atomic_store_n(&g_prof_on, 1, __ATOMIC_RELEASE);
    prof_sampler_begin();
    LOG("[prof] capture started (buffer %u MB)\n", (unsigned)((uint64_t)pool_chunks * CHUNK_RECS * sizeof(ProfRec) >> 20));
    return true;
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put64(FILE *f, uint64_t v) { fwrite(&v, 8, 1, f); }

static void *writer_main(void *arg) {
    (void)arg;
    prof_sampler_end();
    static ProfModule modules[1024];
    int nmod = prof_sampler_modules(modules, 1024);
    // a thread may be in the middle of a record as the capture stops; give it time to finish
    plat_sleep_until_ns(plat_now_ns() + 50000000ull);
    uint32_t used = atomic_load(&next_chunk);
    if (used > pool_chunks) used = pool_chunks;
    unsigned nt = atomic_load(&nthreads);
    if (nt > MAX_THREADS) nt = MAX_THREADS;

    char dir[] = "profile", path[256];
    struct tm tmv;
    time_t t0 = (time_t)unix0;
#ifdef _WIN32
    _mkdir(dir);
    localtime_s(&tmv, &t0);
#else
    mkdir(dir, 0755);
    localtime_r(&t0, &tmv);
#endif
    snprintf(path, sizeof path, "%s/swprof-%04d%02d%02d-%02d%02d%02d.swprof", dir, tmv.tm_year + 1900, tmv.tm_mon + 1,
             tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    // two captures in the same second must not overwrite each other: -2, -3, ...
    for (int n = 2; n < 100; n++) {
        FILE *e = fopen(path, "rb");
        if (!e) break;
        fclose(e);
        snprintf(path, sizeof path, "%s/swprof-%04d%02d%02d-%02d%02d%02d-%d.swprof", dir, tmv.tm_year + 1900,
                 tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, n);
    }
    FILE *f = fopen(path, "wb");
    uint64_t records = 0;
    if (!f) {
        LOG("[prof] cannot write %s\n", path);
    } else {
        fwrite("SWPROF01", 1, 8, f);
        put32(f, FILE_VERSION);
        put32(f, sizeof(ProfRec));
        put64(f, ticks0);
        put64(f, ns0);
        put64(f, ticks1);
        put64(f, ns1);
        put64(f, unix0);
        put32(f, nsites);
        put32(f, nt);
        put32(f, used);
        put32(f, (uint32_t)nmod);
        for (uint32_t i = 1; i <= nsites; i++) {
            const char *n = site_names[i] ? site_names[i] : "?";
            put32(f, i);
            uint16_t len = (uint16_t)strlen(n);
            fwrite(&len, 2, 1, f);
            fwrite(n, 1, len, f);
        }
        for (unsigned i = 0; i < nt; i++) {
            put32(f, i);
            put32(f, 0);
            put64(f, threads[i].tid);
            fwrite(threads[i].name, 1, sizeof threads[i].name, f);
        }
        for (int i = 0; i < nmod; i++) {
            put64(f, modules[i].base);
            put64(f, modules[i].size);
            put64(f, modules[i].bias);
            put32(f, modules[i].kind);
            fwrite(modules[i].name, 1, sizeof modules[i].name, f);
        }
        for (uint32_t i = 0; i < used; i++) {
            ProfRec *r = pool + (size_t)i * CHUNK_RECS;
            uint32_t n = 0;
            while (n < CHUNK_RECS && __atomic_load_n(&r[n].kind, __ATOMIC_ACQUIRE)) n++;
            put32(f, chunk_thread[i]);
            put32(f, n);
            fwrite(r, sizeof(ProfRec), n, f);
            records += n;
        }
        fclose(f);
        snprintf(last_file, sizeof last_file, "%s", path);
        LOG("[prof] wrote %s: %llu events, %u threads, %.2f s\n", path, (unsigned long long)records, nt,
            (double)(ns1 - ns0) / 1e9);
    }
    last_records = records;
    // back to zero for the next capture (only what was used)
    memset(pool, 0, (size_t)used * CHUNK_RECS * sizeof(ProfRec));
    atomic_store(&state, IDLE);
    return NULL;
}

void prof_stop(void) {
    int cap = CAPTURING;
    if (!atomic_compare_exchange_strong(&state, &cap, WRITING)) return;
    __atomic_store_n(&g_prof_on, 0, __ATOMIC_RELEASE);
    ticks1 = prof_ticks();
    ns1 = plat_now_ns();
    pthread_t th;
    if (pthread_create(&th, NULL, writer_main, NULL) == 0) pthread_detach(th);
    else writer_main(NULL);
}

bool prof_toggle(void) {
    if (prof_active()) {
        prof_stop();
        return false;
    }
    return prof_start();
}

void prof_status(char *out, size_t n) {
    int s = atomic_load(&state);
    uint32_t used = atomic_load(&next_chunk);
    if (used > pool_chunks) used = pool_chunks;
    if (s == CAPTURING)
        snprintf(out, n, "capturing for %.1f s, buffer %u of %u chunks used, %u threads",
                 (double)(plat_now_ns() - ns0) / 1e9, used, pool_chunks, atomic_load(&nthreads));
    else if (s == WRITING)
        snprintf(out, n, "writing the capture");
    else
        snprintf(out, n, "idle%s%s", last_file[0] ? ", last file " : "", last_file);
}

// `prof [start | stop | toggle | status | report]`, for any front end that has words (the control layer's command, a
// console): it needs nothing from them, they only hand over the arguments and show the text.
bool prof_command(int argc, char **argv, char *out, size_t n) {
    const char *a = argc > 1 ? argv[1] : "status";
    if (argc > 2) {
        snprintf(out, n, "Usage: prof [start | stop | toggle | status | report]");
        return false;
    }
    if (!strcmp(a, "start")) {
        if (prof_start()) {
            snprintf(out, n, "capture started (prof stop, or F6, ends it)");
            return true;
        }
        char st[256];
        prof_status(st, sizeof st);
        snprintf(out, n, "cannot start: %s", st);
        return false;
    }
    if (!strcmp(a, "stop")) {
        if (!prof_active()) {
            snprintf(out, n, "no capture is running");
            return false;
        }
        prof_stop();
        snprintf(out, n, "capture stopped, writing the file (prof status shows when it is done)");
        return true;
    }
    if (!strcmp(a, "toggle")) {
        bool was = prof_active();
        bool now = prof_toggle();
        if (now) snprintf(out, n, "capture started");
        else if (was) snprintf(out, n, "capture stopped, writing the file");
        else snprintf(out, n, "cannot start now (prof status says why)");
        return now || was;
    }
    if (!strcmp(a, "status")) {
        prof_status(out, n);
        return true;
    }
    if (!strcmp(a, "report")) {
        if (!last_file[0]) {
            snprintf(out, n, "no capture has been written yet");
            return false;
        }
        snprintf(out, n, "python3 tools/swprof.py %s --exe <the swr binary>", last_file);
        return true;
    }
    snprintf(out, n, "Unknown argument %s. Usage: prof [start | stop | toggle | status | report]", a);
    return false;
}
