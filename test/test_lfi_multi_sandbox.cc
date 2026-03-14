#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::LFI> sb1("./testlib.lfi");
    sbox::Sandbox<sbox::LFI> sb2("./testlib2.lfi");
#include "test_multi_sandbox.inc.cc"
    TEST_SUMMARY();
}
