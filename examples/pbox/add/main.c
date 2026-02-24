#include "pbox.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    struct PBox *box = pbox_create("./add_pbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    printf("Sandbox created (pid %d)\n\n", pbox_pid(box));

    // Look up the add function
    void *add_fn = pbox_dlsym(box, "add");
    printf("add function at %p\n", add_fn);

    if (!add_fn) {
        fprintf(stderr, "Failed to find 'add' symbol\n");
        pbox_destroy(box);
        return 1;
    }

    // Call add(2, 3) using convenience macro
    int64_t result = pbox_call2(box, add_fn, int64_t, PBOX_TYPE_SINT64,
                                int64_t, PBOX_TYPE_SINT64, 2,
                                int64_t, PBOX_TYPE_SINT64, 3);
    printf("add(2, 3) = %ld\n", result);

    // Call add(100, 200)
    result = pbox_call2(box, add_fn, int64_t, PBOX_TYPE_SINT64,
                        int64_t, PBOX_TYPE_SINT64, 100,
                        int64_t, PBOX_TYPE_SINT64, 200);
    printf("add(100, 200) = %ld\n\n", result);

    pbox_destroy(box);

    return 0;
}
