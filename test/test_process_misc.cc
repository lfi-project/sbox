#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sbox/process.hh"
#include "test_helpers.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./test_sandbox");
#include "test_misc.inc.cc"
    TEST_SUMMARY();
}
