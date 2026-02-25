#pragma once

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>


// -- Struct definitions (must match testlib.c) --

struct Point {
    int x;
    int y;
};

struct Complex {
    double real;
    double imag;
};

struct NamedArray {
    char* name;
    int* values;
    int count;
};

// -- Test runner macros --

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)               \
    do {                         \
        tests_run++;             \
        printf("  %-50s", name); \
    } while (0)

#define PASS()            \
    do {                  \
        tests_passed++;   \
        printf("PASS\n"); \
    } while (0)

#define TEST_SUMMARY()                                             \
    do {                                                           \
        printf("\n%d/%d tests passed\n", tests_passed, tests_run); \
        assert(tests_passed == tests_run);                         \
    } while (0)
