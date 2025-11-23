// gc.h - Abstract garbage collector interface
#ifndef GC_H
#define GC_H

#include <stddef.h>

// Initialize the garbage collector. Must be called before any allocation.
void gc_init(void);

// Allocate memory managed by the GC. Returns a pointer to a block of at least `size` bytes.
void *gc_allocate(size_t size);

// Perform a garbage collection cycle. For this stub implementation it may be a no-op.
void gc_collect(void);

// Optional: free a specific allocation (not typical for tracing GCs).
void gc_free(void *ptr);

#endif // GC_H
