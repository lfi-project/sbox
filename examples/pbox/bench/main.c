#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "pbox.h"

static struct PBox *box;
static void *add_fn;

#define CALL_ITERATIONS 1000000
#define THREAD_ITERATIONS 1000

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *thread_fn(void *arg) {
    (void)arg;
    int result = pbox_call2(box, add_fn, int, PBOX_TYPE_SINT32,
                            int, PBOX_TYPE_SINT32, 1,
                            int, PBOX_TYPE_SINT32, 2);
    (void)result;
    return NULL;
}

int main(void) {
    box = pbox_create("./bench_pbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    add_fn = pbox_dlsym(box, "add");
    if (!add_fn) {
        fprintf(stderr, "Failed to find add symbol\n");
        pbox_destroy(box);
        return 1;
    }

    // Warmup
    for (int i = 0; i < 100; i++) {
        pbox_call2(box, add_fn, int, PBOX_TYPE_SINT32,
                   int, PBOX_TYPE_SINT32, 1,
                   int, PBOX_TYPE_SINT32, 2);
    }

    // Benchmark 1: Cost of calling add (reusing existing channel)
    {
        double start = now();
        for (int i = 0; i < CALL_ITERATIONS; i++) {
            int result = pbox_call2(box, add_fn, int, PBOX_TYPE_SINT32,
                                    int, PBOX_TYPE_SINT32, i,
                                    int, PBOX_TYPE_SINT32, i + 1);
            (void)result;
        }
        double elapsed = now() - start;
        double per_call_us = (elapsed / CALL_ITERATIONS) * 1e6;
        printf("pbox_call (existing channel): %.3f us/call (%d iterations)\n",
               per_call_us, CALL_ITERATIONS);
    }

    // Benchmark 2: Cost of thread create + call + exit
    {
        double start = now();
        for (int i = 0; i < THREAD_ITERATIONS; i++) {
            pthread_t t;
            pthread_create(&t, NULL, thread_fn, NULL);
            pthread_join(t, NULL);
        }
        double elapsed = now() - start;
        double per_thread_us = (elapsed / THREAD_ITERATIONS) * 1e6;
        printf("thread create + call + exit:  %.3f us/iter (%d iterations)\n",
               per_thread_us, THREAD_ITERATIONS);
    }

    pbox_destroy(box);
    return 0;
}
