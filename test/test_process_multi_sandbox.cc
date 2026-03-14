#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sb1("./test_sandbox");
    sbox::Sandbox<sbox::Process> sb2("./test_sandbox2");
#include "test_multi_sandbox.inc.cc"
    TEST_SUMMARY();
}
