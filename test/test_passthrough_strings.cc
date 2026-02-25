#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sandbox("./libtestlib.so");
#include "test_strings.inc.cc"
    TEST_SUMMARY();
}
