#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::LFI> sandbox("./testlib.lfi");
#include "test_misc.inc.cc"
    TEST_SUMMARY();
}
