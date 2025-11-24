// mark_sweep.c - Mark-and-sweep GC backend
#include "gc_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Allocation headers are woven into a doubly-linked list so we can sweep
// without separate metadata structures.
typedef struct GcHeader
{
    struct GcHeader *prev;
    struct GcHeader *next;
    size_t size;
    int marked;
    gc_trace_func trace;
} GcHeader;

// Roots are stored as slots (addresses of Value*/Env* pointers) to avoid
// guessing lifespan — the interpreter registers/unregisters them.
typedef struct
{
    void **slot;
} GcRoot;

// Global state for the backend; a single mark/sweep heap is shared by all
// allocations.
static GcHeader *gc_objects = NULL;
static size_t gc_bytes_allocated = 0;
static size_t gc_next_threshold = 1024 * 1024; // 1 MB default trigger
static const double GC_GROWTH_FACTOR = 2.0;
static GcRoot *gc_roots = NULL;
static size_t gc_root_count = 0;
static size_t gc_root_capacity = 0;
static int gc_initialized = 0;
static int gc_collecting = 0;
static GcStats internal_stats = {0, 0, 0, 0};

static void ms_collect(void);

static GcHeader *gc_header_for(void *ptr)
{
    if (!ptr)
        return NULL;
    return ((GcHeader *)ptr) - 1;
}

static GcHeader *gc_find_header(void *ptr)
{
    if (!ptr)
        return NULL;
    for (GcHeader *h = gc_objects; h; h = h->next)
    {
        if ((void *)(h + 1) == ptr)
            return h;
    }
    return NULL;
}

// Simple dynamic array helper for root slots.
static void gc_roots_reserve(size_t capacity)
{
    if (gc_root_capacity >= capacity)
        return;
    size_t new_cap = gc_root_capacity ? gc_root_capacity * 2 : 32;
    while (new_cap < capacity)
        new_cap *= 2;
    GcRoot *new_roots = (GcRoot *)realloc(gc_roots, new_cap * sizeof(GcRoot));
    if (!new_roots)
    {
        fprintf(stderr, "GC: failed to grow root set\n");
        exit(1);
    }
    gc_roots = new_roots;
    gc_root_capacity = new_cap;
}

// Backend entry points ------------------------------------------------------

static void ms_init(void)
{
    if (gc_initialized)
        return;
    gc_initialized = 1;
    gc_objects = NULL;
    gc_roots = NULL;
    gc_root_count = 0;
    gc_root_capacity = 0;
    gc_bytes_allocated = 0;
    gc_next_threshold = 1024 * 1024;
    memset(&internal_stats, 0, sizeof(internal_stats));
}

static void *ms_allocate(size_t size)
{
    if (!gc_initialized)
    {
        fprintf(stderr, "GC not initialized. Call gc_init() first.\n");
        exit(1);
    }
    // Header is stored directly before the payload; this keeps pointer updates cheap.
    GcHeader *header = (GcHeader *)malloc(sizeof(GcHeader) + size);
    if (!header)
    {
        fprintf(stderr, "GC allocation failed for %zu bytes\n", size);
        exit(1);
    }
    header->prev = NULL;
    header->next = gc_objects;
    if (gc_objects)
        gc_objects->prev = header;
    gc_objects = header;
    header->size = size;
    header->marked = 0;
    header->trace = NULL;
    void *payload = (void *)(header + 1);
    memset(payload, 0, size);
    gc_bytes_allocated += size;
    internal_stats.allocated_bytes += size;
    internal_stats.current_bytes += size;

    // Trigger opportunistic collection once we exceed the rolling threshold.
    if (!gc_collecting && gc_bytes_allocated > gc_next_threshold)
    {
        ms_collect();
        gc_next_threshold = (size_t)(gc_bytes_allocated * GC_GROWTH_FACTOR);
    }
    return payload;
}

static void ms_set_trace(void *ptr, gc_trace_func trace)
{
    if (!ptr)
        return;
    GcHeader *header = gc_header_for(ptr);
    header->trace = trace;
}

static void *ms_mark_ptr(void *ptr)
{
    if (!ptr)
        return NULL;
    GcHeader *header = gc_find_header(ptr);
    if (!header)
        return NULL;
    if (!header->marked)
    {
        header->marked = 1;
        if (header->trace)
        {
            header->trace(ptr);
        }
    }
    return ptr;
}

static void ms_add_root(void **slot)
{
    if (!slot)
        return;
    for (size_t i = 0; i < gc_root_count; ++i)
    {
        if (gc_roots[i].slot == slot)
            return;
    }
    gc_roots_reserve(gc_root_count + 1);
    gc_roots[gc_root_count++].slot = slot;
}

static void ms_remove_root(void **slot)
{
    if (!slot)
        return;
    for (size_t i = 0; i < gc_root_count; ++i)
    {
        if (gc_roots[i].slot == slot)
        {
            gc_roots[i] = gc_roots[gc_root_count - 1];
            gc_root_count--;
            return;
        }
    }
}

// Mark phase walks all root slots and recursively invokes trace hooks.
static void gc_mark_roots(void)
{
    for (size_t i = 0; i < gc_root_count; ++i)
    {
        void *ptr = *(gc_roots[i].slot);
        if (ptr)
            ms_mark_ptr(ptr);
    }
}

// Sweep passes through the allocation list, unlinking any unmarked object and
// compacting stats as we go. Mark bits are cleared for the next cycle.
static void gc_sweep(void)
{
    GcHeader *obj = gc_objects;
    while (obj)
    {
        GcHeader *next = obj->next;
        if (!obj->marked)
        {
            if (obj->prev)
                obj->prev->next = obj->next;
            else
                gc_objects = obj->next;
            if (obj->next)
                obj->next->prev = obj->prev;

            internal_stats.freed_bytes += obj->size;
            internal_stats.current_bytes -= obj->size;
            free(obj);
        }
        else
        {
            obj->marked = 0;
        }
        obj = next;
    }
}

static void ms_write_barrier(void *owner, void **slot, void *child)
{
    (void)owner;
    (void)slot;
    (void)child;
    // Mark-sweep doesn't require a remembered set; barrier is a no-op.
}

static void ms_collect(void)
{
    if (!gc_initialized || gc_collecting)
        return;
    gc_collecting = 1;
    internal_stats.collections++;
    gc_mark_roots();
    gc_sweep();
    gc_next_threshold = (size_t)(gc_bytes_allocated * GC_GROWTH_FACTOR + 1024);
    gc_collecting = 0;
}

static void ms_free(void *ptr)
{
    if (!ptr)
        return;
    GcHeader *header = gc_header_for(ptr);
    if (header->prev)
        header->prev->next = header->next;
    else
        gc_objects = header->next;
    if (header->next)
        header->next->prev = header->prev;

    internal_stats.freed_bytes += header->size;
    internal_stats.current_bytes -= header->size;
    free(header);
}

static void ms_set_threshold(size_t bytes)
{
    if (bytes < 1024)
        bytes = 1024;
    gc_next_threshold = bytes;
    if (gc_bytes_allocated > gc_next_threshold)
    {
        gc_collect();
    }
}

static size_t ms_get_threshold(void)
{
    return gc_next_threshold;
}

static void ms_get_stats(GcStats *out_stats)
{
    if (out_stats)
    {
        *out_stats = internal_stats;
    }
}

static double ms_get_collections_count(void) { return (double)internal_stats.collections; }
static double ms_get_allocated_bytes(void) { return (double)internal_stats.allocated_bytes; }
static double ms_get_freed_bytes(void) { return (double)internal_stats.freed_bytes; }
static double ms_get_current_bytes(void) { return (double)internal_stats.current_bytes; }

const GcBackend *gc_mark_sweep_backend(void)
{
    static const GcBackend backend = {
        ms_init,
        ms_allocate,
        ms_set_trace,
        ms_mark_ptr,
        ms_add_root,
        ms_remove_root,
        ms_write_barrier,
        ms_collect,
        ms_free,
        ms_set_threshold,
        ms_get_threshold,
        ms_get_stats,
        ms_get_collections_count,
        ms_get_allocated_bytes,
        ms_get_freed_bytes,
        ms_get_current_bytes};
    return &backend;
}
