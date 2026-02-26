#include <cstdio>

#ifdef SBOX_PROCESS
#include "sbox/process.hh"
#else
#include "sbox/passthrough.hh"
#endif

// Host callback function - will be called by sandbox code
static int my_adder(int a, int b) {
    printf("[HOST callback] adding %d + %d = %d\n", a, b, a + b);
    return a + b;
}

int main() {
#ifdef SBOX_PROCESS
    sbox::Sandbox<sbox::Process> sandbox("./callback_sandbox");
#else
    sbox::Sandbox<sbox::Passthrough> sandbox("./libcallback.so");
#endif

    // Register callback
    auto add_fn = sandbox.register_callback(my_adder);
    printf("Registered callback\n");

    // Call sandbox function, passing callback
    using ProcessFn = int(int, int (*)(int, int));
    int result = sandbox.call<ProcessFn>("process_data", 42, add_fn);

    printf("process_data returned: %d\n", result);

    return 0;
}
