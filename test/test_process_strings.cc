#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");
#include "test_strings.inc.cc"
    TEST_SUMMARY();
}
