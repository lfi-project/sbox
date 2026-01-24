#include <cstdio>
#include <ctime>

#ifdef SBOX_PROCESS
#include "sbox/process.hh"
#else
#include "sbox/passthrough.hh"
#endif

#define CALL_ITERATIONS 1000000

static double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
#ifdef SBOX_PROCESS
    sbox::Sandbox<sbox::Process> sandbox("./bench_sandbox");
    const char* backend_name = "process";
#else
    sbox::Sandbox<sbox::Passthrough> sandbox("./libbench.so");
    const char* backend_name = "passthrough";
#endif

    // Get function handle
    auto add = sandbox.fn<int(int, int)>("add");

    // Warmup
    for (int i = 0; i < 100; i++) {
        add(1, 2);
    }

    // Benchmark
    double start = now();
    for (int i = 0; i < CALL_ITERATIONS; i++) {
        int result = add(i, i + 1);
        (void)result;
    }
    double elapsed = now() - start;
    double per_call_us = (elapsed / CALL_ITERATIONS) * 1e6;

    printf("sbox call (%s): %.3f us/call (%d iterations)\n",
           backend_name, per_call_us, CALL_ITERATIONS);

    return 0;
}
