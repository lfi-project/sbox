#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");
#include "test_fn_handles.inc.cc"
    TEST_SUMMARY();
}
