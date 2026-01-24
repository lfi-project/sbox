#include <cstdio>
#include <cstring>
#include <cassert>

#include "sbox/process.hh"

void my_callback(int x) {
    printf("Callback called with: %d\n", x);
}

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");

    printf("Sandbox created (pid %d)\n\n", sandbox.pid());

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

    // Test allocation and copy_to/copy_from
    char* buf = sandbox.alloc<char>(256);
    const char* msg = "Hello, sandbox!";
    sandbox.copy_to(buf, msg, strlen(msg) + 1);

    char* echoed = sandbox.call<char*(char*)>("process_string", buf);

    char result_buf[256];
    sandbox.copy_from(result_buf, echoed, strlen(msg) + 1);
    printf("process_string: %s\n", result_buf);
    assert(strcmp(result_buf, "Hello, sandbox!") == 0);
    sandbox.free(buf);

    // Test copy_string helper
    char* str = sandbox.copy_string("test string");
    char* echoed2 = sandbox.call<char*(char*)>("process_string", str);
    char result_buf2[256];
    sandbox.copy_from(result_buf2, echoed2, 12);
    printf("process_string: %s\n", result_buf2);
    assert(strcmp(result_buf2, "test string") == 0);
    sandbox.free(str);

    // Test callback
    void* cb = sandbox.register_callback(my_callback);
    sandbox.call<void(void(*)(int))>("set_callback", cb);
    sandbox.call<void(int)>("trigger_callback", 123);

    printf("\nAll tests passed!\n");
    return 0;
}
