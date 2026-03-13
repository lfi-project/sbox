#include "sbox/passthrough.hh"

using SboxType = sbox::Passthrough;

#include "test_callbacks.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<SboxType> sandbox("./libtestlib.so");
#include "test_callbacks.inc.cc"
    TEST_SUMMARY();
}
