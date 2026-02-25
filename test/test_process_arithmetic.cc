#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");
#include "test_arithmetic.inc.cc"
    TEST_SUMMARY();
}
