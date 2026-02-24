#include "pbox.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(void) {
    struct PBox *box = pbox_create("./blocked_pbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    printf("Sandbox created (pid %d)\n\n", pbox_pid(box));

    // First, verify the sandbox is working with a safe function
    void *add_fn = pbox_dlsym(box, "add");
    if (!add_fn) {
        fprintf(stderr, "Failed to find 'add' symbol\n");
        pbox_destroy(box);
        return 1;
    }

    int result = pbox_call2(box, add_fn, int, PBOX_TYPE_SINT32,
                            int, PBOX_TYPE_SINT32, 2,
                            int, PBOX_TYPE_SINT32, 3);
    printf("Safe call: add(2, 3) = %d\n\n", result);

    // Now try to call a function that opens a file - this should be blocked
    void *try_open_fn = pbox_dlsym(box, "try_open");
    if (!try_open_fn) {
        fprintf(stderr, "Failed to find 'try_open' symbol\n");
        pbox_destroy(box);
        return 1;
    }

    // Allocate a string in the sandbox for the path
    const char *path = "/etc/passwd";
    size_t path_len = strlen(path) + 1;
    char *sandbox_path = pbox_malloc(box, path_len);
    if (!sandbox_path) {
        fprintf(stderr, "Failed to allocate memory in sandbox\n");
        pbox_destroy(box);
        return 1;
    }

    // Copy the path to sandbox memory
    pbox_copy_to(box, sandbox_path, path, path_len);

    printf("Attempting to open '%s' in sandbox...\n", path);
    printf("(This should be blocked by seccomp)\n\n");

    // This call will cause the sandbox to be killed by seccomp
    // because open() is not in the allowed syscall list
    result = pbox_call1(box, try_open_fn, int, PBOX_TYPE_SINT32,
                        char*, PBOX_TYPE_POINTER, sandbox_path);

    // We likely won't reach here if seccomp kills the process
    if (pbox_alive(box)) {
        if (result < 0) {
            printf("try_open returned error: %d (%s)\n", -result, strerror(-result));
        } else {
            printf("try_open succeeded (unexpected!)\n");
        }
    } else {
        printf("Sandbox died (killed by seccomp as expected)\n");
    }

    pbox_free(box, sandbox_path);
    pbox_destroy(box);

    printf("\nDone!\n");
    return 0;
}
