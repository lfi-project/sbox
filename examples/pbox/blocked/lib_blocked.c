#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// Try to open a file - this should be blocked by seccomp
int try_open(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }
    close(fd);
    return 0;
}

// A safe function that doesn't do any blocked syscalls
int add(int a, int b) {
    return a + b;
}
