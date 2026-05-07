// Demonstrates M1: closure stub addresses are reused across sandbox worker
// threads, so a callback registered on host thread A and invoked through a
// sandbox call running on host thread B's worker dispatches the *wrong*
// callback (whichever one B's worker happened to register).

#include "sbox/process.hh"
#include "test_helpers.hh"

#include <pthread.h>
#include <atomic>

using sbox::Process;
using cb_t = int (*)(int, int);

static int my_add_100(int a, int b) { return a + b + 100; }
static int my_add_200(int a, int b) { return a + b + 200; }

static sbox::Sandbox<Process>* g_sandbox;
static std::atomic<cb_t> g_cb_a{nullptr};
static std::atomic<cb_t> g_cb_b{nullptr};
static std::atomic<int> g_xthread_result{0};

// Host thread T2: register cbB (on this thread's worker), then invoke cbA's
// stub address (registered earlier from the main thread) through a sandbox
// call that also runs on this thread's worker.
static void* t2_register_then_invoke_a(void*) {
    auto cb_b = g_sandbox->register_callback(my_add_200);
    g_cb_b.store(cb_b.unsafe_unverified());

    auto cb_a = sbox::sbox<cb_t>(g_cb_a.load());
    int r = g_sandbox->call<int(cb_t, int, int)>(
        "apply_binary_callback", cb_a, 5, 7);
    g_xthread_result.store(r);
    return nullptr;
}

int main() {
    sbox::Sandbox<Process> sandbox("./test_sandbox");
    g_sandbox = &sandbox;

    // Register cbA on the main thread.
    auto cb_a = sandbox.register_callback(my_add_100);
    g_cb_a.store(cb_a.unsafe_unverified());

    // Sanity: cbA produces 100+a+b when invoked from the registering thread.
    int sanity = sandbox.call<int(cb_t, int, int)>(
        "apply_binary_callback", cb_a, 1, 2);
    assert(sanity == 1 + 2 + 100);

    pthread_t t2;
    pthread_create(&t2, nullptr, t2_register_then_invoke_a, nullptr);
    pthread_join(t2, nullptr);

    TEST("closures registered on different host threads have distinct addresses");
    // With M1 bug: both equal stub_table[0].
    assert(g_cb_a.load() != g_cb_b.load());
    PASS();

    TEST("invoking cbA from a different host thread's worker dispatches cbA");
    // cbA: 5 + 7 + 100 = 112 (correct)
    // cbB: 5 + 7 + 200 = 212 (M1 bug: T2's worker has cbB in slot 0, so the
    //                         stub address — which is also stub_table[0] —
    //                         dispatches cbB instead of cbA)
    assert(g_xthread_result.load() == 112);
    PASS();

    TEST_SUMMARY();
}
