// Lifecycle regressions run in separate processes so environment activation and signal state start fresh.
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "prof.h"
#include "kernel.h"

static pthread_t sampled;
static int sample_main;
uint64_t plat_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
void plat_sleep_until_ns(uint64_t t) { while (plat_now_ns() < t) usleep(100); }
int kernel_running_threads(pthread_t *h, uint32_t *k, uint64_t *e, int max) {
    if (!sample_main || !max) return 0;
    h[0] = sampled; k[0] = 1; e[0] = 0;
    return 1;
}
static void idle(void) {
    char s[256];
    for (int i = 0; i < 5000; i++) {
        prof_status(s, sizeof s);
        if (!strncmp(s, "idle", 4)) return;
        usleep(1000);
    }
    assert(!"writer did not finish");
}
static atomic_int entered, release_scope;
static void *old_scope(void *arg) {
    (void)arg;
    {
        SW_ZONE("old zone");
        SW_ACC("old accumulator");
        atomic_store(&entered, 1);
        while (!atomic_load(&release_scope)) usleep(100);
    }
    // A later marker flushes the completed old accumulator if cleanup wrongly admitted it to the new capture.
    prof_frame(11);
    SW_TALLY("new tally");
    return NULL;
}
static void *one_total(void *arg) {
    (void)arg;
    SW_TALLY("final total");
    return NULL;
}
static uint32_t gpu_gen, gpu_site;
static uint64_t gpu_start;
static void finish_gpu(void) {
    assert(!prof_enabled());
    prof_put_gpu(gpu_gen, gpu_site, gpu_start, gpu_start + 1, 1);
}
static atomic_bool stress_done;
static void *producer(void *arg) {
    (void)arg;
    while (!atomic_load(&stress_done)) {
        SW_ZONE("concurrent zone");
        SW_TALLY("concurrent total");
        SW_COUNT("queue", 7);
    }
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("SAVAGEWIND_PROF_MB", "8", 1);
    setenv("SAVAGEWIND_PROF_SAMPLE", "0", 1);
    if (!strcmp(argv[1], "pending") || !strcmp(argv[1], "pending-rt")) {
        sigset_t set;
        sigemptyset(&set); sigaddset(&set, !strcmp(argv[1], "pending") ? SIGPROF : SIGRTMIN + 6);
        pthread_sigmask(SIG_BLOCK, &set, NULL);
        sampled = pthread_self(); sample_main = 1;
        setenv("SAVAGEWIND_PROF_SAMPLE", "1000", 1);
        assert(prof_start()); usleep(80000); prof_stop(); idle();
        pthread_sigmask(SIG_UNBLOCK, &set, NULL);
        puts("survived signal shutdown");
        return 0;
    }
    if (!strcmp(argv[1], "stress")) {
        pthread_t th[4];
        for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&th[i], NULL, producer, NULL));
        for (unsigned i = 0; i < 20; i++) {
            assert(prof_start()); prof_frame(i * 2); usleep(1000); prof_stop(); idle();
        }
        atomic_store(&stress_done, true);
        for (unsigned i = 0; i < 4; i++) pthread_join(th[i], NULL);
    } else if (!strcmp(argv[1], "exit")) {
        assert(prof_start()); prof_frame(1);
        SW_TALLY("exit total");
        return 0;  // the normal-exit hook must finish the capture
    } else if (!strcmp(argv[1], "gpu-tail")) {
        assert(prof_start()); prof_frame(1);
        gpu_gen = prof_generation(); gpu_site = prof_site_dyn("delayed GPU"); gpu_start = prof_ticks();
        prof_gpu_finish_hook(finish_gpu);
        prof_stop(); idle();
    } else if (!strcmp(argv[1], "one")) {
        setenv("SAVAGEWIND_PROF", "100:1", 1);
        prof_frame(100); usleep(10000); prof_frame(101); idle();
    } else if (!strcmp(argv[1], "cross")) {
        assert(prof_start()); prof_frame(1);
        pthread_t th;
        assert(!pthread_create(&th, NULL, old_scope, NULL));
        while (!atomic_load(&entered)) usleep(100);
        prof_stop(); idle();
        assert(prof_start()); prof_frame(10);
        atomic_store(&release_scope, 1); pthread_join(th, NULL);
        prof_stop(); idle();
    } else if (!strcmp(argv[1], "final")) {
        assert(prof_start()); prof_frame(1);
        pthread_t th;
        assert(!pthread_create(&th, NULL, one_total, NULL));
        pthread_join(th, NULL);
        prof_stop(); idle();
    } else if (!strcmp(argv[1], "write-failure")) {
        FILE *f = fopen("profile", "w"); assert(f); fclose(f);
        assert(prof_start()); prof_frame(1); prof_stop(); idle();
        assert(!prof_last_file()[0]);
        unlink("profile");
        assert(prof_start()); prof_frame(2); prof_stop(); idle();
    } else return 2;
    printf("FILE %s\n", prof_last_file());
    return 0;
}
