#include <cstdio>
#include <cassert>
#include <pthread.h>

#if defined(SBOX_PROCESS)
#include "sbox/process.hh"
using SboxType = sbox::Sandbox<sbox::Process>;
#elif defined(SBOX_LFI)
#include "sbox/lfi.hh"
using SboxType = sbox::Sandbox<sbox::LFI>;
#else
#include "sbox/passthrough.hh"
using SboxType = sbox::Sandbox<sbox::Passthrough>;
#endif

static SboxType* sandbox;

#define NUM_THREADS 4
#define ITERATIONS 100

static void* thread_fn(void* arg) {
    int id = static_cast<int>(reinterpret_cast<intptr_t>(arg));

    // Set TLS to thread-specific value
    int tls_base = (id + 1) * 1000;
    sandbox->call<void(int)>("set_tls", tls_base);

    // Verify TLS was set correctly
    int tls_val = sandbox->call<int()>("get_tls");
    assert(tls_val == tls_base);

    for (int i = 0; i < ITERATIONS; i++) {
        int a = id * 1000 + i;
        int b = i;

        int result = sandbox->call<int(int, int)>("slow_add", a, b);
        assert(result == a + b);

        // Increment TLS and verify it persists
        int new_tls = sandbox->call<int()>("increment_tls");
        assert(new_tls == tls_base + i + 1);
    }

    // Final TLS check
    tls_val = sandbox->call<int()>("get_tls");
    assert(tls_val == tls_base + ITERATIONS);

    printf("Thread %d completed %d iterations (TLS: %d -> %d)\n",
           id, ITERATIONS, tls_base, tls_val);
    return nullptr;
}

int main() {
#if defined(SBOX_PROCESS)
    SboxType sb("./threads_sandbox");
#elif defined(SBOX_LFI)
    SboxType sb("./libthreads.lfi");
#else
    SboxType sb("./libthreads.so");
#endif
    sandbox = &sb;

    // Quick sanity check on main thread
    int result = sandbox->call<int(int, int)>("add", 10, 20);
    printf("Main thread: add(10, 20) = %d\n", result);
    assert(result == 30);

    // Test TLS on main thread
    sandbox->call<void(int)>("set_tls", 42);
    int tls = sandbox->call<int()>("get_tls");
    printf("Main thread: TLS set to %d, read back %d\n", 42, tls);
    assert(tls == 42);

    // Spawn threads
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], nullptr, thread_fn, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }

    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    // Verify main thread TLS wasn't affected by other threads
    tls = sandbox->call<int()>("get_tls");
    printf("Main thread: TLS after threads = %d (expected 42)\n", tls);
    assert(tls == 42);

    printf("All threads completed successfully!\n");

    return 0;
}
