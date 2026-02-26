#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "pbox.h"

int main(void) {
    struct PBox* box = pbox_create("./identity_pbox_sandbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    // Create identity-mapped shared memory
    size_t size = 4096;
    int* shared = pbox_mmap_identity(box, size, PROT_READ | PROT_WRITE);
    if (!shared) {
        fprintf(stderr, "Failed to create shared memory\n");
        pbox_destroy(box);
        return 1;
    }

    printf("Shared memory at %p\n", (void*) shared);

    // Write from host
    shared[0] = 42;
    shared[1] = 100;

    // Call sandbox to read and verify at the same address
    void* verify_fn = pbox_dlsym(box, "verify_and_modify");
    if (!verify_fn) {
        fprintf(stderr, "Failed to find verify_and_modify\n");
        pbox_munmap_identity(box, shared, size);
        pbox_destroy(box);
        return 1;
    }

    // Pass the pointer directly - it's valid in both processes
    int result =
        pbox_call2(box, verify_fn, int, PBOX_TYPE_SINT32, void*,
                   PBOX_TYPE_POINTER, shared, size_t, PBOX_TYPE_UINT64, size);

    if (result != 0) {
        fprintf(stderr, "Sandbox verification failed\n");
        pbox_munmap_identity(box, shared, size);
        pbox_destroy(box);
        return 1;
    }

    // Verify sandbox modifications from host
    if (shared[0] != 84 || shared[1] != 200) {
        fprintf(stderr, "Host verification failed: got %d, %d\n", shared[0],
                shared[1]);
        pbox_munmap_identity(box, shared, size);
        pbox_destroy(box);
        return 1;
    }

    printf("Identity-mapped shared memory works!\n");
    printf("  Host wrote: 42, 100\n");
    printf("  Sandbox doubled: %d, %d\n", shared[0], shared[1]);

    pbox_munmap_identity(box, shared, size);
    pbox_destroy(box);
    return 0;
}
