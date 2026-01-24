#include <cstdio>

#ifdef SBOX_PROCESS
#include "sbox/process.hh"
#else
#include "sbox/passthrough.hh"
#endif

int main() {
#ifdef SBOX_PROCESS
    sbox::Sandbox<sbox::Process> sandbox("./add_sandbox");
#else
    sbox::Sandbox<sbox::Passthrough> sandbox("./libadd.so");
#endif

    // Call add(2, 3)
    int result = sandbox.call<int(int, int)>("add", 2, 3);
    printf("add(2, 3) = %d\n", result);

    // Call add(100, 200)
    result = sandbox.call<int(int, int)>("add", 100, 200);
    printf("add(100, 200) = %d\n", result);

    // Use function handle for repeated calls
    auto add = sandbox.fn<int(int, int)>("add");
    result = add(1000, 2000);
    printf("add(1000, 2000) = %d\n", result);

    return 0;
}
