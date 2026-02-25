#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sandbox("./libtestlib.so");
#include "test_memory.inc.cc"
    TEST_SUMMARY();
}
