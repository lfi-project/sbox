#define SBOX_SKIP_STACK_ARG_CALLBACKS
#include "sbox/lfi.hh"

using SboxType = sbox::LFI;

#include "test_callbacks.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<SboxType> sandbox("./testlib.lfi");
#include "test_callbacks.inc.cc"
    TEST_SUMMARY();
}
