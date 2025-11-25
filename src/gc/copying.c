// copying.c - Copying GC backend 
#include "gc_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Simple semi-space copying collector: allocations occur in the active space,
// collections copy reachable objects into the inactive space, then swap.
#define DEFAULT_COPY_HEAP (32 * 1024 * 1024)

// Each object is tagged with a payload size, trace hook, and forwarding pointer
// (used during the copy phase to avoid duplicating an object).
typedef struct CopyHeader {
    size_t size;
    gc_trace_func trace;
    void *forward;
    unsigned char tag;
} CopyHeader;

// Roots are stored as addresses of Value/Env/Tokens so we can update slots
// in place when objects move to the new semi-space.
typedef struct {
    void **slot;
} CopyRoot;

// Global copying-collector state. We keep two semi-spaces of equal size and
// alternate between them whenever a collection occurs.
static unsigned char *active_space = NULL;
static unsigned char *inactive_space = NULL;
static size_t semi_space_size = DEFAULT_COPY_HEAP;
static unsigned char *alloc_ptr = NULL;
static unsigned char *alloc_end = NULL;
static int copying_initialized = 0;
static int copying_collecting = 0;
static CopyRoot *copy_roots = NULL;
static size_t copy_root_count = 0;
static size_t copy_root_capacity = 0;
static GcStats copy_stats = {0, 0, 0, 0};

static size_t align_size(size_t size) {
    size_t align = sizeof(void*);
    return (size + align - 1) & ~(align - 1);
}

// Helpers --------------------------------------------------------------

static int pointer_in_space(unsigned char *space, void *ptr) {
    return ptr && (unsigned char*)ptr > space && (unsigned char*)ptr < space + semi_space_size;
}

static void copy_reset_roots(void) {
    copy_root_count = 0;
    copy_root_capacity = 0;
    free(copy_roots);
    copy_roots = NULL;
}

static void copy_alloc_spaces(size_t size) {
    if (active_space) free(active_space);
    if (inactive_space) free(inactive_space);
    semi_space_size = size ? align_size(size) : DEFAULT_COPY_HEAP;
    active_space = (unsigned char*)malloc(semi_space_size);
    inactive_space = (unsigned char*)malloc(semi_space_size);
    if (!active_space || !inactive_space) {
        fprintf(stderr, "Copying GC: failed to allocate heap (%zu bytes)\n", semi_space_size);
        exit(1);
    }
    alloc_ptr = active_space;
    alloc_end = active_space + semi_space_size;
}

static void copy_init(void) {
    if (copying_initialized) return;
    copy_alloc_spaces(semi_space_size);
    copy_reset_roots();
    memset(&copy_stats, 0, sizeof(copy_stats));
    // Timing fields are zero-initialized by memset
    copying_initialized = 1;
}

static CopyHeader *copy_header_for(void *ptr) {
    if (!ptr) return NULL;
    return ((CopyHeader*)ptr) - 1;
}

static void copy_roots_reserve(size_t needed) {
    if (copy_root_capacity >= needed) return;
    size_t new_cap = copy_root_capacity ? copy_root_capacity * 2 : 32;
    while (new_cap < needed) new_cap *= 2;
    CopyRoot *roots = (CopyRoot*)realloc(copy_roots, new_cap * sizeof(CopyRoot));
    if (!roots) {
        fprintf(stderr, "Copying GC: failed to grow root set\n");
        exit(1);
    }
    copy_roots = roots;
    copy_root_capacity = new_cap;
}

static void copy_add_root(void **slot) {
    if (!slot) return;
    copy_roots_reserve(copy_root_count + 1);
    copy_roots[copy_root_count++].slot = slot;
}

static void copy_remove_root(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < copy_root_count; ++i) {
        if (copy_roots[i].slot == slot) {
            copy_roots[i] = copy_roots[copy_root_count - 1];
            copy_root_count--;
            return;
        }
    }
}

// Allocation/trace API -------------------------------------------------

static void copy_collect(void);
static void *copy_copy_ptr(void *ptr);

static void *copy_allocate(size_t size) {
    if (!copying_initialized) copy_init();
    size_t payload = align_size(size);
    size_t total = sizeof(CopyHeader) + payload;
    if (alloc_ptr + total > alloc_end) {
        copy_collect();
        if (alloc_ptr + total > alloc_end) {
            fprintf(stderr, "Copying GC: out of memory (requested %zu bytes). Increase gc-threshold.\n", size);
            exit(1);
        }
    }
    CopyHeader *header = (CopyHeader*)alloc_ptr;
    alloc_ptr += total;
    header->size = payload;
    header->trace = NULL;
    header->forward = NULL;
    header->tag = GC_TAG_UNKNOWN;
    void *payload_ptr = (void*)(header + 1);
    memset(payload_ptr, 0, payload);
    copy_stats.allocated_bytes += size;
    copy_stats.current_bytes = alloc_ptr - active_space;
    return payload_ptr;
}

static void copy_set_trace(void *ptr, gc_trace_func trace) {
    CopyHeader *header = copy_header_for(ptr);
    if (header) header->trace = trace;
}

static void copy_set_tag(void *ptr, unsigned char tag) {
    if (!ptr) return;
    if (pointer_in_space(active_space, ptr) || pointer_in_space(inactive_space, ptr)) {
        CopyHeader *header = copy_header_for(ptr);
        if (header) header->tag = tag;
    }
}

static void copy_write_barrier(void *owner, void **slot, void *child) {
    (void)owner;
    (void)slot;
    (void)child;
}

static void swap_spaces(void) {
    unsigned char *tmp = inactive_space;
    inactive_space = active_space;
    active_space = tmp;
    alloc_ptr = active_space;
    alloc_end = active_space + semi_space_size;
}

// Copy helpers ---------------------------------------------------------

static void *copy_copy_ptr(void *ptr) {
    if (!ptr) return NULL;
    CopyHeader *old_header = copy_header_for(ptr);
    if (!old_header) return NULL;
    if (!pointer_in_space(inactive_space, old_header + 1)) {
        return ptr; // already in to-space
    }
    if (old_header->forward) return old_header->forward;
    size_t total = sizeof(CopyHeader) + old_header->size;
    if (alloc_ptr + total > alloc_end) {
        fprintf(stderr, "Copying GC: insufficient semispace (current %zu bytes). Increase gc-threshold.\n", semi_space_size);
        exit(1);
    }
    CopyHeader *new_header = (CopyHeader*)alloc_ptr;
    alloc_ptr += total;
    new_header->size = old_header->size;
    new_header->trace = old_header->trace;
    new_header->forward = NULL;
    new_header->tag = old_header->tag;
    memcpy(new_header + 1, old_header + 1, old_header->size);
    old_header->forward = new_header + 1;
    copy_stats.objects_copied++;  // Track copied objects
    return old_header->forward;
}

static void scan_active_space(void) {
    unsigned char *scan = active_space;
    size_t scanned = 0;
    while (scan < alloc_ptr) {
        CopyHeader *header = (CopyHeader*)scan;
        void *obj = (void*)(header + 1);
        scanned++;
        if (header->trace) header->trace(obj);
        scan += sizeof(CopyHeader) + header->size;
    }
    copy_stats.objects_scanned += scanned;
}

static void copy_collect(void) {
    if (!copying_initialized || copying_collecting) return;
    copying_collecting = 1;
    
    // Start timing
    double start_time = gc_get_time_ms();
    
    size_t before = copy_stats.current_bytes;
    copy_stats.collections++;
    
    // Track objects before collection for survival rate
    size_t objects_before_copy = copy_stats.objects_copied;
    
    swap_spaces();
    for (size_t i = 0; i < copy_root_count; ++i) {
        void **slot = copy_roots[i].slot;
        if (slot && *slot) {
            *slot = copy_copy_ptr(*slot);
        }
    }
    scan_active_space();
    size_t after = alloc_ptr - active_space;
    copy_stats.current_bytes = after;
    if (before > after) copy_stats.freed_bytes += before - after;
    
    // Calculate survival rate (copied objects / scanned objects)
    size_t objects_copied_this_cycle = copy_stats.objects_copied - objects_before_copy;
    if (copy_stats.objects_scanned > 0) {
        copy_stats.survival_rate = (double)objects_copied_this_cycle / (double)copy_stats.objects_scanned;
    }
    
    // Calculate metadata overhead: count live objects in active space
    size_t live_objects = 0;
    unsigned char *scan = active_space;
    while (scan < alloc_ptr) {
        CopyHeader *header = (CopyHeader*)scan;
        live_objects++;
        scan += sizeof(CopyHeader) + header->size;
    }
    copy_stats.metadata_bytes = live_objects * sizeof(CopyHeader);
    
    // End timing and update stats
    double elapsed = gc_get_time_ms() - start_time;
    copy_stats.last_gc_pause_ms = elapsed;
    copy_stats.total_gc_time_ms += elapsed;
    if (elapsed > copy_stats.max_gc_pause_ms) {
        copy_stats.max_gc_pause_ms = elapsed;
    }
    copy_stats.avg_gc_pause_ms = copy_stats.total_gc_time_ms / copy_stats.collections;
    
    copying_collecting = 0;
}

static void *copy_mark_ptr(void *ptr) {
    if (!copying_collecting) return ptr;
    return copy_copy_ptr(ptr);
}

static void copy_free(void *ptr) {
    (void)ptr;
    // No-op; objects are reclaimed during collection.
}

static void copy_set_threshold(size_t bytes) {
    (void)bytes;
    // Runtime resizing not supported yet.
}

static size_t copy_get_threshold(void) {
    return semi_space_size;
}

static void copy_get_stats(GcStats *out_stats) {
    if (out_stats) *out_stats = copy_stats;
}

static double copy_get_collections_count(void) { return (double)copy_stats.collections; }
static double copy_get_allocated_bytes(void) { return (double)copy_stats.allocated_bytes; }
static double copy_get_freed_bytes(void) { return (double)copy_stats.freed_bytes; }
static double copy_get_current_bytes(void) { return (double)copy_stats.current_bytes; }

static size_t copy_heap_snapshot(GcObjectInfo *out, size_t capacity) {
    size_t count = 0;
    unsigned char *scan = active_space;
    while (scan < alloc_ptr && count < capacity) {
        CopyHeader *header = (CopyHeader*)scan;
        out[count].addr = (uintptr_t)(header + 1);
        out[count].size = header->size;
        out[count].generation = GC_GEN_NURSERY;
        out[count].tag = header->tag;
        count++;
        scan += sizeof(CopyHeader) + header->size;
    }
    return count;
}

const GcBackend *gc_copying_backend(void) {
    static const GcBackend backend = {
        copy_init,
        copy_allocate,
        copy_set_trace,
        copy_mark_ptr,
        copy_set_tag,
        copy_add_root,
        copy_remove_root,
        copy_write_barrier,
        copy_collect,
        copy_free,
        copy_set_threshold,
        copy_get_threshold,
        copy_get_stats,
        copy_get_collections_count,
        copy_get_allocated_bytes,
        copy_get_freed_bytes,
        copy_get_current_bytes,
        copy_heap_snapshot
    };
    return &backend;
}
