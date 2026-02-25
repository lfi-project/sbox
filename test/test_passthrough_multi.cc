#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sandbox("./libtestlib.so");

    printf("== Multiple sandbox instances ==\n");

    TEST("two sandboxes independently");
    {
        sbox::Sandbox<sbox::Passthrough> sandbox2("./libtestlib.so");
        int r1 = sandbox.call<int(int, int)>("add", 1, 2);
        int r2 = sandbox2.call<int(int, int)>("add", 3, 4);
        assert(r1 == 3);
        assert(r2 == 7);
    }
    PASS();

    TEST_SUMMARY();
}
