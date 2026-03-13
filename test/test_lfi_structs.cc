#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::LFI> sandbox("./testlib.lfi");
#include "test_structs.inc.cc"
    TEST_SUMMARY();
}
