#include <stdio.h>
#include <string.h>
#include "pbox.h"

// Host callback function - will be called by sandbox code
// Uses integers only to avoid pointer translation issues
static int my_adder(int a, int b) {
    printf("[HOST callback] adding %d + %d = %d\n", a, b, a + b);
    return a + b;
}

// Dispatch function: unpacks args from storage and calls the real function
static void my_adder_dispatch(pbox_fn_t func_ptr, const char* arg_storage,
                               const uint64_t* arg_offsets,
                               char* result_storage) {
    int (*fn)(int, int) = (int (*)(int, int))func_ptr;
    int a, b;
    memcpy(&a, &arg_storage[arg_offsets[0]], sizeof(int));
    memcpy(&b, &arg_storage[arg_offsets[1]], sizeof(int));
    int result = fn(a, b);
    memcpy(result_storage, &result, sizeof(int));
}

int main(void) {
    struct PBox* box = pbox_create("./callback_pbox_sandbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    // Register callback - get a function pointer usable in the sandbox
    enum PBoxType arg_types[] = {PBOX_TYPE_SINT32, PBOX_TYPE_SINT32};
    void* add_fn = pbox_register_callback(box, (pbox_fn_t)my_adder,
                                           my_adder_dispatch,
                                           PBOX_TYPE_SINT32, 2, arg_types);

    if (!add_fn) {
        fprintf(stderr, "Failed to register callback\n");
        pbox_destroy(box);
        return 1;
    }

    printf("Registered callback at sandbox address %p\n", add_fn);

    // Look up sandbox function
    void* process = pbox_dlsym(box, "process_data");
    if (!process) {
        fprintf(stderr, "Failed to find process_data\n");
        pbox_destroy(box);
        return 1;
    }

    // Call sandbox function, passing callback as a regular function pointer
    int result =
        pbox_call2(box, process, int, PBOX_TYPE_SINT32, int, PBOX_TYPE_SINT32,
                   42, void*, PBOX_TYPE_POINTER, add_fn);

    printf("process_data returned: %d\n", result);

    pbox_destroy(box);
    return 0;
}
