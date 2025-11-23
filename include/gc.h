// gc.h - Abstract garbage collector interface
#ifndef GC_H
#define GC_H

#include <stddef.h>

typedef void (*gc_trace_func)(void *object);

typedef struct {
    size_t collections;
    size_t allocated_bytes;
    size_t freed_bytes;
    size_t current_bytes;
} GcStats;

// Initialize the garbage collector. Must be called before any allocation.
void gc_init(void);

// Allocate memory managed by the GC. Returns a pointer to a block of at least `size` bytes.
void *gc_allocate(size_t size);

// Assign a trace function to the allocation so the collector can follow references.
void gc_set_trace(void *ptr, gc_trace_func trace);

// Mark a GC-managed pointer as reachable (used by trace callbacks).
void *gc_mark_ptr(void *ptr);

// Register/unregister a root pointer slot.
void gc_add_root(void **slot);
void gc_remove_root(void **slot);

// Adjust/get the automatic GC threshold in bytes.
void gc_set_threshold(size_t bytes);
size_t gc_get_threshold(void);

// Get current GC statistics.
void gc_get_stats(GcStats *out_stats);

// Helper getters for WASM binding
double gc_get_collections_count(void);
double gc_get_allocated_bytes(void);
double gc_get_freed_bytes(void);
double gc_get_current_bytes(void);

// Perform a garbage collection cycle.
void gc_collect(void);

// Optional: free a specific allocation (not typical for tracing GCs).
void gc_free(void *ptr);

#endif // GC_H
