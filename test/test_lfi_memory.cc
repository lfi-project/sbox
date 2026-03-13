#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::LFI> sandbox("./testlib.lfi");
#include "test_memory.inc.cc"
    TEST_SUMMARY();
}
