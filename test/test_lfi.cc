#include <cstdio>
#include <cstring>
#include <cassert>

#include "sbox/lfi.hh"

void my_callback(int x) {
    printf("Callback called with: %d\n", x);
}

int main() {
    sbox::Sandbox<sbox::LFI> sandbox("./testlib.lfi");

    // Test basic function call
    int result = sandbox.call<int(int, int)>("add", 10, 32);
    printf("add(10, 32) = %d\n", result);
    assert(result == 42);

    // Test another function
    result = sandbox.call<int(int, int)>("multiply", 6, 7);
    printf("multiply(6, 7) = %d\n", result);
    assert(result == 42);

    // Test function handle for repeated calls
    auto add = sandbox.fn<int(int, int)>("add");
    result = add(100, 200);
    printf("add(100, 200) = %d\n", result);
    assert(result == 300);

    // Test allocation
    char* buf = sandbox.alloc<char>(256);
    strcpy(buf, "Hello, sandbox!");
    char* echoed = sandbox.call<char*(char*)>("process_string", buf);
    printf("process_string: %s\n", echoed);
    assert(strcmp(echoed, "Hello, sandbox!") == 0);
    sandbox.free(buf);

    // Test callback
    auto cb = sandbox.register_callback(my_callback);
    sandbox.call<void(void(*)(int))>("set_callback", cb);
    sandbox.call<void(int)>("trigger_callback", 123);

    printf("\nAll tests passed!\n");
    return 0;
}
