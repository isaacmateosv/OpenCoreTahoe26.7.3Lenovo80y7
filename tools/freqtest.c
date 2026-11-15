// freqtest: measures the effective CPU clock under load without sudo.
// A chain of dependent 64-bit adds retires exactly 1 per cycle on Intel cores,
// so (adds executed) / (elapsed time) = clock frequency of the core running it.
// Phase 1: 1 thread for 6 s (single-core turbo). Phase 2: N threads for 40 s
// (all-core turbo, then the sustained clock once the PL2 burst budget runs out).
// Always terminates on its own. Build: clang -O2 -pthread -o freqtest freqtest.c
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static mach_timebase_info_data_t tb;
static double now_s(void) { return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9; }

#define ADDS_PER_BLOCK 10000
static inline void block(void) {
    __asm__ volatile(
        "xor %%rax, %%rax\n"
        "mov $1, %%rcx\n"
        ".rept 10000\n addq %%rcx, %%rax\n .endr\n" ::: "rax", "rcx", "cc");
}

#define MAXT 16
static atomic_ullong blocks[MAXT];
static atomic_int stop;

static void *worker(void *arg) {
    int id = (int)(long)arg;
    while (!atomic_load_explicit(&stop, memory_order_relaxed)) {
        block();
        atomic_fetch_add_explicit(&blocks[id], 1, memory_order_relaxed);
    }
    return NULL;
}

static void run(int nthreads, int seconds, const char *label) {
    pthread_t t[MAXT];
    atomic_store(&stop, 0);
    for (int i = 0; i < nthreads; i++) { atomic_store(&blocks[i], 0); pthread_create(&t[i], NULL, worker, (void *)(long)i); }
    unsigned long long prev[MAXT] = {0};
    double t0 = now_s(), last = t0;
    printf("%s (%d thread%s)\n", label, nthreads, nthreads > 1 ? "s" : "");
    for (int s = 1; s <= seconds; s++) {
        usleep(1000000);
        double tnow = now_s(), dt = tnow - last; last = tnow;
        double sum = 0, mn = 1e9, mx = 0;
        for (int i = 0; i < nthreads; i++) {
            unsigned long long b = atomic_load(&blocks[i]);
            double ghz = (double)(b - prev[i]) * ADDS_PER_BLOCK / dt / 1e9;
            prev[i] = b; sum += ghz; if (ghz < mn) mn = ghz; if (ghz > mx) mx = ghz;
        }
        printf("  t=%2ds  avg %.2f GHz  (min %.2f, max %.2f)\n", s, sum / nthreads, mn, mx);
        fflush(stdout);
    }
    atomic_store(&stop, 1);
    for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
}

int main(int argc, char **argv) {
    mach_timebase_info(&tb);
    int n = argc > 1 ? atoi(argv[1]) : 8; if (n < 1 || n > MAXT) n = 8;
    run(1, 6, "Phase 1: single-core turbo");
    sleep(2);
    run(n, 40, "Phase 2: all-core load");
    return 0;
}
