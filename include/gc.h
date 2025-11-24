// gc.h - Abstract garbage collector interface
#ifndef GC_H
#define GC_H

#include <stddef.h>
#include <stdint.h>

typedef void (*gc_trace_func)(void *object);

enum {
    GC_GEN_UNKNOWN = 0,
    GC_GEN_NURSERY = 1,
    GC_GEN_OLD = 2
};

enum {
    GC_TAG_UNKNOWN = 0,
    GC_TAG_VALUE_NUMBER = 1,
    GC_TAG_VALUE_SYMBOL = 2,
    GC_TAG_VALUE_PAIR = 3,
    GC_TAG_VALUE_LAMBDA = 4,
    GC_TAG_VALUE_BUILTIN = 5,
    GC_TAG_ENV = 10,
    GC_TAG_BINDING = 11,
    GC_TAG_STRING = 12
};

typedef struct {
    uintptr_t addr;
    size_t size;
    unsigned char generation;
    unsigned char tag;
} GcObjectInfo;

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

// Tag an allocation (used for visualization/diagnostics).
void gc_set_tag(void *ptr, unsigned char tag);

// Register/unregister a root pointer slot.
void gc_add_root(void **slot);
void gc_remove_root(void **slot);

// Inform the GC that `owner` now references `child` via `slot`.
void gc_write_barrier(void *owner, void **slot, void *child);

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

// Snapshot current heap objects. Returns number of entries written.
size_t gc_heap_snapshot(GcObjectInfo *out, size_t capacity);
size_t gc_heap_snapshot_flat(uint32_t *out, size_t capacity);
size_t gc_heap_snapshot_entry_size(void);
size_t gc_heap_snapshot_addr_offset(void);
size_t gc_heap_snapshot_size_offset(void);
size_t gc_heap_snapshot_generation_offset(void);
size_t gc_heap_snapshot_tag_offset(void);

// Perform a garbage collection cycle.
void gc_collect(void);

// Optional: free a specific allocation (not typical for tracing GCs).
void gc_free(void *ptr);

#endif // GC_H
