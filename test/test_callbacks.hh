#pragma once

// Callback functions and state used by test_callbacks.inc.cc

static int callback_value = 0;

static void my_callback(int x) {
    callback_value = x;
}

static int my_add_callback(int a, int b) {
    return a + b;
}

static int my_multiply_callback(int a, int b) {
    return a * b;
}

static double my_double_callback(double val) {
    return val * 2.0;
}

static double my_square_callback(double val) {
    return val * val;
}

static int my_quad_callback(int a, int b, int c, int d) {
    return a * b + c * d;
}
