#include "sbox/process.hh"

using SboxType = sbox::Process;

#include "test_callbacks.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<SboxType> sandbox("./test_sandbox");
#include "test_callbacks.inc.cc"
    TEST_SUMMARY();
}
