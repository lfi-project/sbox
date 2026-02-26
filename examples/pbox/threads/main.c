#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "pbox.h"

static struct PBox* box;
static void* add_fn;
static void* slow_add_fn;

#define NUM_THREADS 4
#define ITERATIONS 100

static void* thread_fn(void* arg) {
    int id = (int) (intptr_t) arg;

    for (int i = 0; i < ITERATIONS; i++) {
        int a = id * 1000 + i;
        int b = i;

        int result = pbox_call2(box, slow_add_fn, int, PBOX_TYPE_SINT32, int,
                                PBOX_TYPE_SINT32, a, int, PBOX_TYPE_SINT32, b);

        assert(result == a + b);
    }

    printf("Thread %d completed %d iterations\n", id, ITERATIONS);
    return NULL;
}

int main(void) {
    box = pbox_create("./threads_pbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    add_fn = pbox_dlsym(box, "add");
    slow_add_fn = pbox_dlsym(box, "slow_add");

    if (!add_fn || !slow_add_fn) {
        fprintf(stderr, "Failed to find symbols\n");
        pbox_destroy(box);
        return 1;
    }

    // Quick sanity check on main thread
    int result = pbox_call2(box, add_fn, int, PBOX_TYPE_SINT32, int,
                            PBOX_TYPE_SINT32, 10, int, PBOX_TYPE_SINT32, 20);
    printf("Main thread: add(10, 20) = %d\n", result);
    assert(result == 30);

    // Spawn threads
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_fn, (void*) (intptr_t) i);
    }

    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads completed successfully!\n");

    pbox_destroy(box);
    return 0;
}
