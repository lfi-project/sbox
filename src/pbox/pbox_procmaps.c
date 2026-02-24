#define _GNU_SOURCE

#include "pbox_procmaps.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// Parse /proc/<pid>/maps to get mapped regions
// Returns array of (start, end) pairs
static uintptr_t *parse_proc_maps(pid_t pid, size_t *count) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    size_t cap = 64;
    size_t n = 0;
    uintptr_t *regions = malloc(cap * 2 * sizeof(uintptr_t));
    if (!regions) {
        fclose(f);
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uintptr_t start, end;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
            if (n >= cap) {
                cap *= 2;
                uintptr_t *new_regions = realloc(regions, cap * 2 * sizeof(uintptr_t));
                if (!new_regions) {
                    free(regions);
                    fclose(f);
                    return NULL;
                }
                regions = new_regions;
            }
            regions[n * 2] = start;
            regions[n * 2 + 1] = end;
            n++;
        }
    }

    fclose(f);
    *count = n;
    return regions;
}

// Check if [addr, addr+len) overlaps with any region
static int range_overlaps(uintptr_t addr, size_t len, uintptr_t *regions, size_t count) {
    uintptr_t end = addr + len;
    for (size_t i = 0; i < count; i++) {
        uintptr_t r_start = regions[i * 2];
        uintptr_t r_end = regions[i * 2 + 1];
        if (addr < r_end && end > r_start)
            return 1;
    }
    return 0;
}

void *pbox_find_common_free_address(pid_t pid1, pid_t pid2, size_t length) {
    size_t count1, count2;
    uintptr_t *regions1 = parse_proc_maps(pid1, &count1);
    uintptr_t *regions2 = parse_proc_maps(pid2, &count2);

    if (!regions1 || !regions2) {
        free(regions1);
        free(regions2);
        return NULL;
    }

    uintptr_t page_size = 4096;
    uintptr_t align = 64 * 1024;  // 64KB alignment
    length = (length + page_size - 1) & ~(page_size - 1);

    // Candidate base addresses in typical mmap region
    uintptr_t candidates[] = {
        0x700000000000UL,
        0x600000000000UL,
        0x500000000000UL,
        0x400000000000UL,
        0x200000000000UL,
        0x100000000000UL,
    };

    void *result = NULL;
    for (size_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
        uintptr_t base = candidates[c];
        for (uintptr_t offset = 0; offset < 0x10000000000UL; offset += align) {
            uintptr_t addr = base + offset;
            if (!range_overlaps(addr, length, regions1, count1) &&
                !range_overlaps(addr, length, regions2, count2)) {
                result = (void *)addr;
                goto done;
            }
        }
    }

done:
    free(regions1);
    free(regions2);
    return result;
}
