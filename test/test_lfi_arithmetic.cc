#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    auto sb = sbox::Sandbox<sbox::LFI>::create("./testlib.lfi");
    assert(sb);
    auto& sandbox = *sb;
#include "test_arithmetic.inc.cc"
    TEST_SUMMARY();
}
