// Shared callback tests.
// Assumes: sandbox, TEST/PASS macros, test counters, callback functions in
// scope.

{
    printf("== Callbacks ==\n");

    TEST("basic callback (void(int))");
    callback_value = 0;
    void* cb = sandbox.register_callback(my_callback);
    assert(cb != nullptr);
    sandbox.call<void(void (*)(int))>("set_callback", cb);
    sandbox.call<void(int)>("trigger_callback", 42);
    assert(callback_value == 42);
    PASS();

    TEST("callback re-trigger");
    callback_value = 0;
    sandbox.call<void(int)>("trigger_callback", 999);
    assert(callback_value == 999);
    PASS();

    TEST("binary callback (int(int,int)) - add");
    void* add_cb = sandbox.register_callback(my_add_callback);
    assert(add_cb != nullptr);
    int cbr = sandbox.call<int(int (*)(int, int), int, int)>(
        "apply_binary_callback", add_cb, 10, 20);
    assert(cbr == 30);
    PASS();

    TEST("binary callback (int(int,int)) - multiply");
    void* mul_cb = sandbox.register_callback(my_multiply_callback);
    assert(mul_cb != nullptr);
    cbr = sandbox.call<int(int (*)(int, int), int, int)>(
        "apply_binary_callback", mul_cb, 6, 7);
    assert(cbr == 42);
    PASS();

    TEST("double callback (double(double)) - double");
    void* dbl_cb = sandbox.register_callback(my_double_callback);
    assert(dbl_cb != nullptr);
    double dr = sandbox.call<double(double (*)(double), double)>(
        "apply_double_callback", dbl_cb, 3.14);
    assert(fabs(dr - 6.28) < 1e-9);
    PASS();

    TEST("double callback (double(double)) - square");
    void* sq_cb = sandbox.register_callback(my_square_callback);
    assert(sq_cb != nullptr);
    dr = sandbox.call<double(double (*)(double), double)>(
        "apply_double_callback", sq_cb, 5.0);
    assert(fabs(dr - 25.0) < 1e-9);
    PASS();

    TEST("quad callback (int(int,int,int,int))");
    void* quad_cb = sandbox.register_callback(my_quad_callback);
    assert(quad_cb != nullptr);
    cbr = sandbox.call<int(int (*)(int, int, int, int), int, int, int, int)>(
        "apply_quad_callback", quad_cb, 3, 4, 5, 6);
    assert(cbr == 42);  // 3*4 + 5*6 = 12 + 30
    PASS();
}
