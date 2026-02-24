// Sandbox library for identity-shared memory test

#include <stddef.h>

// Verify values written by host and modify them
// Returns 0 on success, -1 on failure
int verify_and_modify(int *shared, size_t size) {
    (void)size;
    // Verify host wrote expected values
    if (shared[0] != 42 || shared[1] != 100)
        return -1;

    // Double the values
    shared[0] *= 2;
    shared[1] *= 2;

    return 0;
}
