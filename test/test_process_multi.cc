#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");

    printf("== Multiple sandbox instances ==\n");

    TEST("two process sandboxes independently");
    {
        sbox::Sandbox<sbox::Process> sandbox2("./test_sandbox");
        assert(sandbox.pid() != sandbox2.pid());
        int r1 = sandbox.call<int(int, int)>("add", 1, 2);
        int r2 = sandbox2.call<int(int, int)>("add", 3, 4);
        assert(r1 == 3);
        assert(r2 == 7);
    }
    PASS();

    TEST_SUMMARY();
}
