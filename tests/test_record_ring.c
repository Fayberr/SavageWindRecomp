// The recorder ring (runtime/nvn/vulkan/record.c), with its own thread and no Vulkan device: the section GC keeps
// what rec_init, rec_UpdateDescriptorSets, rec_drain and rec_stats_take need, and vkUpdateDescriptorSets is this
// file's, so each packet the recorder runs lands here. Packets of varying sizes go through a 1 MB ring (it wraps,
// fills and waits for room many times); the recorder must see every one exactly once, in order, with the bytes it
// was given. Run with SAVAGEWIND_VK_RECORDER_SPIN=0 too (the recorder sleeps whenever it catches up).
// --bench: ns per packet on the translating thread for a stream of small packets.
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nvn/vulkan/vk.h"
#include "nvn/vulkan/record.h"

// ---- what record.c's kept functions call
VkG g_vk;
uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
int plat_cpu_count(void) { return 8; }  // (6 or more: the recorder spins before it sleeps, unless SPIN says otherwise)
void plat_thread_elevate(void) {}
void plat_sleep_until_ns(uint64_t t) {  // (prof.c's sampler)
    while (plat_now_ns() < t) {}
}
void control_prof_thread(const char *name) { (void)name; }
void rt_fatal_trace(void) { abort(); }

// ---- the recorder's side: each packet is one vkUpdateDescriptorSets call
static uint64_t seen;       // packets run (recorder thread; read after rec_drain)
static uint64_t bad;        // packets out of order or with the wrong bytes
static bool bench;
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice dev, uint32_t n, const VkWriteDescriptorSet *w, uint32_t nc,
                                                  const VkCopyDescriptorSet *c) {
    (void)dev; (void)nc; (void)c;
    if (bench) { seen++; return; }
    // packet i has 1 + i % 7 writes; write j carries (i, j) in its binding / array element and its buffer info
    uint64_t i = seen++;
    if (i % 20000 == 10000) {  // the recorder held up (a slow driver call): the translator fills the ring and waits
        uint64_t end = plat_now_ns() + 2000000;
        while (plat_now_ns() < end) {}
    }
    if (n != 1 + i % 7) { bad++; return; }
    for (uint32_t j = 0; j < n; j++) {
        const VkDescriptorBufferInfo *b = w[j].pBufferInfo;
        if (w[j].dstBinding != (uint32_t)i || w[j].dstArrayElement != j || !b || b->offset != i * 8 + j || b->range != ~i)
            bad++;
    }
}

static void send(uint64_t i) {
    VkWriteDescriptorSet w[7];
    VkDescriptorBufferInfo b[7];
    uint32_t n = 1 + (uint32_t)(i % 7);
    for (uint32_t j = 0; j < n; j++) {
        b[j] = (VkDescriptorBufferInfo){VK_NULL_HANDLE, i * 8 + j, ~i};
        w[j] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = (uint32_t)i,
                                      .dstArrayElement = j, .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &b[j]};
    }
    rec_UpdateDescriptorSets(n, w);
}

int main(int argc, char **argv) {
    bench = argc > 1 && !strcmp(argv[1], "--bench");
    if (!getenv("SAVAGEWIND_VK_RECORDER_MB")) {
#ifdef _WIN32
        _putenv_s("SAVAGEWIND_VK_RECORDER_MB", bench ? "16" : "1");
#else
        setenv("SAVAGEWIND_VK_RECORDER_MB", bench ? "16" : "1", 1);
#endif
    }
    rec_init();
    assert(rec_on());

    if (bench) {
        // the smallest packet (one write: 128 bytes), as most of a draw's are
        VkDescriptorBufferInfo b = {VK_NULL_HANDLE, 0, 256};
        VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &b};
        const uint64_t n = 32768 * 600;
        double best = 1e30, best_t = 1e30;
        for (int run = 0; run < 3; run++) {
            seen = 0;
            // in chunks of 4 MB of packets, a quarter of the ring, so the translating thread never waits for room:
            // its time is its own (the recorder runs alongside, as in the game)
            uint64_t tq_ns = 0, t0 = plat_now_ns();
            for (uint64_t i = 0; i < n; i += 32768) {
                uint64_t c0 = plat_now_ns();
                for (uint64_t j = 0; j < 32768; j++) rec_UpdateDescriptorSets(1, &w);
                tq_ns += plat_now_ns() - c0;
                rec_drain();
            }
            double ns = (double)(plat_now_ns() - t0) / (double)n, ns_t = (double)tq_ns / (double)n;
            if (ns < best) best = ns;
            if (ns_t < best_t) best_t = ns_t;
            assert(seen == n);
        }
        printf("recorder ring: %.2f ns per packet queued (translating thread), %.2f ns per packet recorded (best of 3, %llu packets)\n",
               best_t, best, (unsigned long long)n);
        return 0;
    }

    // 1 MB ring, packets of 128-656 bytes (~390 on average): ~2700 fill it, so 3 M packets wrap it ~1100 times
    const uint64_t n = 3000000;
    for (uint64_t i = 0; i < n; i++) {
        send(i);
        if (i % 500000 == 499999) {  // a drain now and then, as at a CPU wait for a submission
            rec_drain();
            assert(seen == i + 1);
        }
    }
    rec_drain();
    RecStats rs;
    rec_stats_take(&rs);
    printf("packets %llu, seen %llu, bad %llu, ring-full waits %llu, drains %llu\n", (unsigned long long)rs.packets,
           (unsigned long long)seen, (unsigned long long)bad, (unsigned long long)rs.full_waits, (unsigned long long)rs.drains);
    assert(seen == n);
    assert(bad == 0);
    assert(rs.packets == n);
    assert(rs.full_waits > 0);  // (the stalls above filled it)
    puts("PASS: every packet recorded once, in order, intact");
    return 0;
}
