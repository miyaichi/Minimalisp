// gc.c - Simple stub garbage collector implementation
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>

static int gc_initialized = 0;

void gc_init(void) {
    // For now, just mark initialized. Real implementations would set up heap structures.
    gc_initialized = 1;
    // Optionally, could pre-allocate a pool.
}

void *gc_allocate(size_t size) {
    if (!gc_initialized) {
        fprintf(stderr, "GC not initialized. Call gc_init() first.\n");
        exit(1);
    }
    // Simple wrapper around malloc for now.
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "GC allocation failed for %zu bytes\n", size);
        exit(1);
    }
    return ptr;
}

void gc_collect(void) {
    // No-op stub. Real GC would trace reachable objects and free unreachable ones.
    // For demonstration, we just print a message.
    // printf("gc_collect called (no-op)\n");
}

void gc_free(void *ptr) {
    // Optional free for manual deallocation.
    free(ptr);
}
