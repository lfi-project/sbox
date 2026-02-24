#pragma once

#include <stddef.h>
#include <sys/types.h>

// Find an address of given size that is free in both processes
// Returns NULL if no suitable address found
void *pbox_find_common_free_address(pid_t pid1, pid_t pid2, size_t length);
