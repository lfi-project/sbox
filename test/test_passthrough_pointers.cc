#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sandbox("./libtestlib.so");
#include "test_pointers.inc.cc"
    TEST_SUMMARY();
}
