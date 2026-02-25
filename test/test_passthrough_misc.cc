#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sbox/passthrough.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Passthrough> sandbox("./libtestlib.so");
#include "test_misc.inc.cc"
    TEST_SUMMARY();
}
