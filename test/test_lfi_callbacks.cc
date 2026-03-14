#define SBOX_SKIP_STACK_ARG_CALLBACKS
#include "sbox/lfi.hh"

using SboxType = sbox::LFI;

#include "test_callbacks.hh"
#include "test_helpers.hh"

int main() {
    auto sb = sbox::Sandbox<SboxType>::create("./testlib.lfi");
    assert(sb);
    auto& sandbox = *sb;
#include "test_callbacks.inc.cc"
    TEST_SUMMARY();
}
