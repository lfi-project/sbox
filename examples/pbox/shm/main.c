#define _GNU_SOURCE

#include "pbox.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUF_SIZE 4096

int main(void) {
    struct PBox *box = pbox_create("./shm_pbox");
    if (!box) {
        fprintf(stderr, "Failed to create sandbox\n");
        return 1;
    }

    printf("Sandbox created (pid %d)\n\n", pbox_pid(box));

    // Create shared memory using memfd_create
    int memfd = memfd_create("shared_buffer", 0);
    if (memfd < 0) {
        perror("memfd_create");
        pbox_destroy(box);
        return 1;
    }

    if (ftruncate(memfd, BUF_SIZE) < 0) {
        perror("ftruncate");
        close(memfd);
        pbox_destroy(box);
        return 1;
    }

    // Map the buffer in the host
    unsigned char *host_buf = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, memfd, 0);
    if (host_buf == MAP_FAILED) {
        perror("mmap (host)");
        close(memfd);
        pbox_destroy(box);
        return 1;
    }

    // Initialize host buffer to zeros
    memset(host_buf, 0, BUF_SIZE);
    printf("Host buffer initialized to zeros\n");
    printf("First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n\n",
           host_buf[0], host_buf[1], host_buf[2], host_buf[3],
           host_buf[4], host_buf[5], host_buf[6], host_buf[7]);

    // Map the same buffer in the sandbox
    void *sandbox_buf = pbox_mmap(box, NULL, BUF_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, memfd, 0);
    if (sandbox_buf == MAP_FAILED) {
        fprintf(stderr, "pbox_mmap failed\n");
        munmap(host_buf, BUF_SIZE);
        close(memfd);
        pbox_destroy(box);
        return 1;
    }

    printf("Shared buffer mapped in sandbox at %p\n\n", sandbox_buf);

    // Look up the fill_buffer function
    void *fill_fn = pbox_dlsym(box, "fill_buffer");
    if (!fill_fn) {
        fprintf(stderr, "Failed to find 'fill_buffer' symbol\n");
        pbox_destroy(box);
        return 1;
    }

    // Call fill_buffer in the sandbox to fill with 0xAB
    printf("Calling fill_buffer(buf, %d, 0xAB) in sandbox...\n", BUF_SIZE);
    pbox_call3(box, fill_fn, int, PBOX_TYPE_VOID,
               void*, PBOX_TYPE_POINTER, sandbox_buf,
               size_t, PBOX_TYPE_UINT64, (size_t)BUF_SIZE,
               unsigned char, PBOX_TYPE_UINT8, 0xAB);

    printf("First 8 bytes after fill: %02x %02x %02x %02x %02x %02x %02x %02x\n\n",
           host_buf[0], host_buf[1], host_buf[2], host_buf[3],
           host_buf[4], host_buf[5], host_buf[6], host_buf[7]);

    // Look up the increment_buffer function
    void *inc_fn = pbox_dlsym(box, "increment_buffer");
    if (!inc_fn) {
        fprintf(stderr, "Failed to find 'increment_buffer' symbol\n");
        pbox_destroy(box);
        return 1;
    }

    // Call increment_buffer in the sandbox
    printf("Calling increment_buffer(buf, %d) in sandbox...\n", BUF_SIZE);
    pbox_call2(box, inc_fn, int, PBOX_TYPE_VOID,
               void*, PBOX_TYPE_POINTER, sandbox_buf,
               size_t, PBOX_TYPE_UINT64, (size_t)BUF_SIZE);

    printf("First 8 bytes after increment: %02x %02x %02x %02x %02x %02x %02x %02x\n\n",
           host_buf[0], host_buf[1], host_buf[2], host_buf[3],
           host_buf[4], host_buf[5], host_buf[6], host_buf[7]);

    // Verify the shared memory works both ways - modify from host
    printf("Modifying first byte from host to 0xFF...\n");
    host_buf[0] = 0xFF;

    // Read it back via the sandbox (call a function that returns the first byte)
    // For simplicity, we'll just show that the sandbox can see it by calling increment again
    printf("Calling increment_buffer again...\n");
    pbox_call2(box, inc_fn, int, PBOX_TYPE_VOID,
               void*, PBOX_TYPE_POINTER, sandbox_buf,
               size_t, PBOX_TYPE_UINT64, (size_t)BUF_SIZE);

    printf("First 8 bytes after host modify + increment: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           host_buf[0], host_buf[1], host_buf[2], host_buf[3],
           host_buf[4], host_buf[5], host_buf[6], host_buf[7]);
    printf("(First byte wrapped from 0xFF to 0x00)\n\n");

    // Cleanup
    munmap(host_buf, BUF_SIZE);
    close(memfd);
    pbox_destroy(box);

    printf("Done!\n");
    return 0;
}
