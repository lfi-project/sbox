#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sb1("./libtestlib.so");
    sbox::Sandbox<sbox::Passthrough> sb2("./libtestlib2.so");
#include "test_multi_sandbox.inc.cc"
    TEST_SUMMARY();
}
