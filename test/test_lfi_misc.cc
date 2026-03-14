#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sbox/lfi.hh"
#include "test_helpers.hh"

int main() {
    auto sb = sbox::Sandbox<sbox::LFI>::create("./testlib.lfi");
    assert(sb);
    auto& sandbox = *sb;
#include "test_misc.inc.cc"
    TEST_SUMMARY();
}
