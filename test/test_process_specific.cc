#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");

    printf("== Process-specific ==\n");

    TEST("pid() returns valid pid");
    assert(sandbox.pid() > 0);
    PASS();

    TEST("alive() returns true");
    assert(sandbox.alive());
    PASS();

    TEST_SUMMARY();
}
