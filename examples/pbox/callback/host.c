#include "pbox.h"
#include <stdio.h>

// Host callback function - will be called by sandbox code
// Uses integers only to avoid pointer translation issues
static int my_adder(int a, int b) {
    printf("[HOST callback] adding %d + %d = %d\n", a, b, a + b);
    return a + b;
}

int main(void) {
    struct PBox *box = pbox_create("./callback_pbox_sandbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    // Register callback - get a function pointer usable in the sandbox
    void *add_fn = pbox_register_callback2(box, my_adder,
        PBOX_TYPE_SINT32,                      // return type
        PBOX_TYPE_SINT32, PBOX_TYPE_SINT32);   // arg types

    if (!add_fn) {
        fprintf(stderr, "Failed to register callback\n");
        pbox_destroy(box);
        return 1;
    }

    printf("Registered callback at sandbox address %p\n", add_fn);

    // Look up sandbox function
    void *process = pbox_dlsym(box, "process_data");
    if (!process) {
        fprintf(stderr, "Failed to find process_data\n");
        pbox_destroy(box);
        return 1;
    }

    // Call sandbox function, passing callback as a regular function pointer
    int result = pbox_call2(box, process, int, PBOX_TYPE_SINT32,
                            int, PBOX_TYPE_SINT32, 42,
                            void*, PBOX_TYPE_POINTER, add_fn);

    printf("process_data returned: %d\n", result);

    pbox_destroy(box);
    return 0;
}
