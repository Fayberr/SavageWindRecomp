// Tests the Vulkan recorder ring (runtime/nvn/vulkan/record.c) with a real recorder thread:
// cache-line isolation, multi-wrap FIFO ordering, payload integrity across skip packets,
// batched tail publication with cached_tail reloads, backpressure on a full ring,
// rec_drain(), rec_QueueSubmit2() + rec_wait_submitted(), and rec_stats_take().
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Stub runtime/Vulkan dependencies needed by record.c (unused vkCmd* wrappers are GC'd).
#include "../runtime/nvn/vulkan/record.c"

VkG g_vk;
Vk13 g_vk13;
pthread_mutex_t g_vk_queue_lock = PTHREAD_MUTEX_INITIALIZER;
void (*rt_crash_note)(void);
int g_prof_on;
uint32_t g_prof_gen;
uint32_t prof_register(ProfSite *s) { (void)s; return 1; }
void prof_put_gen(uint32_t gen, uint32_t kind, uint32_t site, uint64_t t0, uint64_t t1, uint64_t val) {
    (void)gen; (void)kind; (void)site; (void)t0; (void)t1; (void)val;
}
void prof_acc_add_gen(uint32_t gen, ProfSite *s, uint64_t ticks) { (void)gen; (void)s; (void)ticks; }
void rt_fatal_trace(void) { abort(); }
void vk_device_fault_report(void) {}

uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
int plat_cpu_count(void) { return 8; }
void plat_thread_elevate(void) {}
void control_prof_thread(const char *name) { (void)name; }
void prof_thread_name(const char *name) { (void)name; }
void vk_view_describe(VkImageView v, char *s, size_t n) { (void)v; if (n) s[0] = '\0'; }
void mvec_resolve(uint32_t seq, uint32_t out[MVEC_PUSH / 4]) { (void)seq; memset(out, 0, MVEC_PUSH); }
void vk_devlost_dump(void) {}

// Stubbed Vulkan entry points invoked by the recorder thread during this test.
static _Atomic uint64_t g_draws_seen;
static _Atomic uint64_t g_push_seen;
static _Atomic uint64_t g_submits_seen;
static _Atomic uint64_t g_order_errors;
static _Atomic int g_stall_recorder;

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer cb, uint32_t count, uint32_t inst, uint32_t first, uint32_t first_inst) {
    (void)cb; (void)inst;
    assert(rec_on_recorder());
    uint64_t seq = atomic_fetch_add_explicit(&g_draws_seen, 1, memory_order_relaxed);
    if (count != (uint32_t)seq || first != (uint32_t)(seq ^ 0x5a5a5a5au) || first_inst != (uint32_t)(seq + 7))
        atomic_fetch_add_explicit(&g_order_errors, 1, memory_order_relaxed);
    while (atomic_load_explicit(&g_stall_recorder, memory_order_acquire)) RELAX();
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages,
                                              uint32_t off, uint32_t size, const void *data) {
    (void)cb; (void)layout; (void)stages; (void)off;
    assert(rec_on_recorder());
    uint64_t idx = atomic_fetch_add_explicit(&g_push_seen, 1, memory_order_relaxed);
    const uint32_t *w = (const uint32_t *)data;
    uint32_t n = size / 4;
    for (uint32_t i = 0; i < n; i++) {
        if (w[i] != (uint32_t)(idx * 131u + i)) {
            atomic_fetch_add_explicit(&g_order_errors, 1, memory_order_relaxed);
            break;
        }
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL stub_QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *pSubmits, VkFence fence) {
    (void)queue; (void)count; (void)pSubmits; (void)fence;
    assert(rec_on_recorder());
    atomic_fetch_add_explicit(&g_submits_seen, 1, memory_order_relaxed);
    return VK_SUCCESS;
}

static _Atomic uint64_t g_custom_seen;
static void custom_cb(void *arg) {
    assert(rec_on_recorder());
    uint64_t expected = (uint64_t)(uintptr_t)arg;
    uint64_t got = atomic_fetch_add_explicit(&g_custom_seen, 1, memory_order_relaxed);
    if (got != expected) atomic_fetch_add_explicit(&g_order_errors, 1, memory_order_relaxed);
}

static void check_cacheline_isolation(void) {
    uintptr_t addrs[] = {
        (uintptr_t)&rec_t,
        (uintptr_t)&rec_head,
        (uintptr_t)&rec_r,
        (uintptr_t)&rec_sync,
        (uintptr_t)&mv_state,
        (uintptr_t)&begin_state,
    };
    size_t sizes[] = {
        sizeof rec_t,
        sizeof rec_head,
        sizeof rec_r,
        sizeof rec_sync,
        sizeof mv_state,
        sizeof begin_state,
    };
    size_t n = sizeof addrs / sizeof addrs[0];
    for (size_t i = 0; i < n; i++) {
        assert((addrs[i] & (REC_CACHELINE - 1)) == 0);
        assert((sizes[i] & (REC_CACHELINE - 1)) == 0);
        for (size_t j = i + 1; j < n; j++) {
            uintptr_t a0 = addrs[i], a1 = a0 + sizes[i];
            uintptr_t b0 = addrs[j], b1 = b0 + sizes[j];
            assert(a1 <= b0 || b1 <= a0);
        }
    }
}

typedef struct { uint64_t target; } UnstallArg;
static void *unstall_thread(void *arg) {
    UnstallArg *u = arg;
    // Wait until T has queued enough to hit full-ring backpressure, then release R.
    uint64_t deadline = plat_now_ns() + 200000000ull;
    while (atomic_load_explicit(&rec_sync.done_waiters, memory_order_acquire) == 0 && plat_now_ns() < deadline)
        RELAX();
    (void)u;
    atomic_store_explicit(&g_stall_recorder, 0, memory_order_release);
    return NULL;
}

int main(int argc, char **argv) {
    check_cacheline_isolation();

    // Smallest supported ring (1 MB) so tests force many wrap-arounds and skip packets.
#ifdef _WIN32
    _putenv_s("SAVAGEWIND_VK_RECORDER", "1");
    _putenv_s("SAVAGEWIND_VK_RECORDER_MB", "1");
    _putenv_s("SAVAGEWIND_VK_RECORDER_SPIN", "50");
    _putenv_s("SAVAGEWIND_VK_PROF", "2");
#else
    setenv("SAVAGEWIND_VK_RECORDER", "1", 1);
    setenv("SAVAGEWIND_VK_RECORDER_MB", "1", 1);
    setenv("SAVAGEWIND_VK_RECORDER_SPIN", "50", 1);
    setenv("SAVAGEWIND_VK_PROF", "2", 1);
#endif
    g_vk13.QueueSubmit2 = stub_QueueSubmit2;
    rec_init();
    assert(rec_on());
    assert(!rec_on_recorder());
    assert(ring_cap == (1ull << 20));

    // 1. Multi-wrap FIFO + variable-sized payload integrity across skip packets (>6 MB through 1 MB ring).
    uint32_t payload[64];
    const uint64_t N_DRAWS = 100000;
    uint64_t draws_sent = 0, pushes_sent = 0, customs_sent = 0;
    for (uint64_t i = 0; i < N_DRAWS; i++) {
        rec_CmdDraw(VK_NULL_HANDLE, (uint32_t)draws_sent, 1, (uint32_t)(draws_sent ^ 0x5a5a5a5au), (uint32_t)(draws_sent + 7));
        draws_sent++;
        if ((i & 7) == 0) {
            uint32_t words = 16 + (uint32_t)(i % 48);
            for (uint32_t j = 0; j < words; j++) payload[j] = (uint32_t)(pushes_sent * 131u + j);
            rec_CmdPushConstants(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_SHADER_STAGE_VERTEX_BIT, 0, words * 4, payload);
            pushes_sent++;
        }
        if ((i & 511) == 0) {
            rec_custom(custom_cb, (void *)(uintptr_t)customs_sent++);
        }
    }
    rec_drain();
    assert(atomic_load(&g_draws_seen) == draws_sent);
    assert(atomic_load(&g_push_seen) == pushes_sent);
    assert(atomic_load(&g_custom_seen) == customs_sent);
    assert(atomic_load(&g_order_errors) == 0);
    assert(rec_t.cached_tail == rec_t.wpos);
    assert(rec_t.wpos > 4 * ring_cap);  // proved multiple ring wraps occurred

    // 2. Submissions & rec_wait_submitted().
    VkSubmitInfo2 si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    for (uint64_t seq = 1; seq <= 32; seq++) {
        rec_CmdDraw(VK_NULL_HANDLE, (uint32_t)draws_sent, 1, (uint32_t)(draws_sent ^ 0x5a5a5a5au), (uint32_t)(draws_sent + 7));
        draws_sent++;
        rec_QueueSubmit2(&si, seq);
        rec_wait_submitted(seq);
        assert(rec_submitted() >= seq);
    }
    rec_drain();
    assert(atomic_load(&g_submits_seen) == 32);
    assert(atomic_load(&g_draws_seen) == draws_sent);

    // 3. Full-ring backpressure: stall R on the first packet, overfill the 1 MB ring from T, and verify wakeup.
    atomic_store_explicit(&g_stall_recorder, 1, memory_order_release);
    pthread_t ut;
    UnstallArg ua = {0};
    assert(pthread_create(&ut, NULL, unstall_thread, &ua) == 0);
    for (uint64_t i = 0; i < 40000; i++) {
        rec_CmdDraw(VK_NULL_HANDLE, (uint32_t)draws_sent, 1, (uint32_t)(draws_sent ^ 0x5a5a5a5au), (uint32_t)(draws_sent + 7));
        draws_sent++;
    }
    pthread_join(ut, NULL);
    rec_drain();
    assert(atomic_load(&g_draws_seen) == draws_sent);
    assert(atomic_load(&g_order_errors) == 0);

    RecStats st;
    rec_stats_take(&st);
    assert(st.full_waits >= 1);
    assert(st.packets == draws_sent + pushes_sent + customs_sent + 32);
    assert(st.bytes > 0);

    if (argc > 1 && !strcmp(argv[1], "--bench")) {
        const uint64_t NB = 500000;
        uint64_t t0 = plat_now_ns();
        for (uint64_t i = 0; i < NB; i++) {
            rec_CmdDraw(VK_NULL_HANDLE, (uint32_t)draws_sent, 1, (uint32_t)(draws_sent ^ 0x5a5a5a5au), (uint32_t)(draws_sent + 7));
            draws_sent++;
        }
        rec_drain();
        uint64_t dt = plat_now_ns() - t0;
        printf("bench: %llu packets in %.2f ms (%.1f ns/pkt)\n",
               (unsigned long long)NB, (double)dt / 1e6, (double)dt / (double)NB);
    }

    printf("test_record_ring: ok (%llu packets, %llu bytes)\n",
           (unsigned long long)st.packets, (unsigned long long)st.bytes);
    return 0;
}
