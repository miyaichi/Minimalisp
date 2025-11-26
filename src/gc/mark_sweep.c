// mark_sweep.c - Mark-and-sweep GC backend with Free List Allocator
#include "gc_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Minimum alignment for all allocations
#define ALIGNMENT sizeof(void*)
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define MIN_BLOCK_SIZE (sizeof(FreeHeader))

// Header for free blocks in the free list
typedef struct FreeHeader {
    size_t size;             // Size of the block (including this header)
    struct FreeHeader *next; // Next free block
} FreeHeader;

// Allocation headers are woven into a doubly-linked list so we can sweep
// without separate metadata structures.
typedef struct GcHeader
{
    struct GcHeader *prev;
    struct GcHeader *next;
    size_t size;      // Size of the user payload
    size_t block_size; // Total size of the block (header + payload + padding)
    int marked;
    gc_trace_func trace;
    unsigned char tag;
} GcHeader;

// Roots are stored as slots (addresses of Value*/Env* pointers)
typedef struct
{
    void **slot;
} GcRoot;

// Global state
static uint8_t *heap_start = NULL;
static uint8_t *heap_end = NULL;
static size_t heap_size = 0;
static FreeHeader *free_list = NULL;

static GcHeader *gc_objects = NULL;
static size_t gc_bytes_allocated = 0;
static size_t gc_next_threshold = 1024 * 1024;
static const double GC_GROWTH_FACTOR = 1.5; // Lower growth factor since heap is fixed size
static GcRoot *gc_roots = NULL;
static size_t gc_root_count = 0;
static size_t gc_root_capacity = 0;
static int gc_initialized = 0;
static int gc_collecting = 0;
static GcStats internal_stats = {0, 0, 0, 0};

static void ms_collect(void);

// Helper: Get GcHeader from user pointer
static GcHeader *gc_header_for(void *ptr)
{
    if (!ptr) return NULL;
    return ((GcHeader *)ptr) - 1;
}

// Helper: Find header in object list (safe check)
static GcHeader *gc_find_header(void *ptr)
{
    if (!ptr) return NULL;
    // In this implementation, we trust the pointer arithmetic if it's within heap bounds
    if ((uint8_t*)ptr < heap_start || (uint8_t*)ptr >= heap_end) return NULL;
    return gc_header_for(ptr);
}

// Root management
static void gc_roots_reserve(size_t capacity)
{
    if (gc_root_capacity >= capacity) return;
    size_t new_cap = gc_root_capacity ? gc_root_capacity * 2 : 32;
    while (new_cap < capacity) new_cap *= 2;
    GcRoot *new_roots = (GcRoot *)realloc(gc_roots, new_cap * sizeof(GcRoot));
    if (!new_roots) {
        fprintf(stderr, "GC: failed to grow root set\n");
        exit(1);
    }
    gc_roots = new_roots;
    gc_root_capacity = new_cap;
}

// Allocator Internals -------------------------------------------------------

static void ms_heap_init_allocator(size_t size) {
    heap_size = size;
    heap_start = (uint8_t*)malloc(heap_size);
    if (!heap_start) {
        fprintf(stderr, "GC: Failed to allocate heap of size %zu\n", heap_size);
        exit(1);
    }
    heap_end = heap_start + heap_size;

    // Initialize free list with one large block
    free_list = (FreeHeader*)heap_start;
    free_list->size = heap_size;
    free_list->next = NULL;
}

// Allocate a block from the free list (First-Fit)
static void *ms_heap_alloc(size_t size) {
    size_t needed = ALIGN(size);
    if (needed < MIN_BLOCK_SIZE) needed = MIN_BLOCK_SIZE;

    FreeHeader *prev = NULL;
    FreeHeader *curr = free_list;

    while (curr) {
        if (curr->size >= needed) {
            // Found a suitable block
            if (curr->size >= needed + MIN_BLOCK_SIZE) {
                // Split the block
                FreeHeader *remaining = (FreeHeader*)((uint8_t*)curr + needed);
                remaining->size = curr->size - needed;
                remaining->next = curr->next;
                
                if (prev) prev->next = remaining;
                else free_list = remaining;
                
                curr->size = needed; // Only the allocated part
            } else {
                // Use the whole block (remove from list)
                if (prev) prev->next = curr->next;
                else free_list = curr->next;
            }
            return (void*)curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL; // Out of memory
}

// Free a block and coalesce with neighbors
static void ms_heap_free(void *ptr, size_t size) {
    if (!ptr) return;
    
    FreeHeader *block = (FreeHeader*)ptr;
    block->size = size;
    
    FreeHeader *prev = NULL;
    FreeHeader *curr = free_list;
    
    // Find insertion point (sorted by address)
    while (curr && curr < block) {
        prev = curr;
        curr = curr->next;
    }
    
    // Insert 'block' between 'prev' and 'curr'
    block->next = curr;
    if (prev) prev->next = block;
    else free_list = block;
    
    // Coalesce with next
    if (curr && (uint8_t*)block + block->size == (uint8_t*)curr) {
        block->size += curr->size;
        block->next = curr->next;
    }
    
    // Coalesce with prev
    if (prev && (uint8_t*)prev + prev->size == (uint8_t*)block) {
        prev->size += block->size;
        prev->next = block->next;
    }
}

// Backend entry points ------------------------------------------------------

static void ms_init(void)
{
    if (gc_initialized) return;
    gc_initialized = 1;
    
    size_t initial_size = gc_get_initial_heap_size();
    if (initial_size == 0) initial_size = 4 * 1024 * 1024; // Default 4MB
    ms_heap_init_allocator(initial_size);

    gc_objects = NULL;
    gc_roots = NULL;
    gc_root_count = 0;
    gc_root_capacity = 0;
    gc_bytes_allocated = 0;
    gc_next_threshold = initial_size / 2; // Trigger GC halfway
    
    memset(&internal_stats, 0, sizeof(internal_stats));
    internal_stats.peak_fragmentation_index = 0.0;
    internal_stats.fragmentation_growth_rate = 0.0;
}

static void *ms_allocate(size_t size)
{
    if (!gc_initialized) ms_init();

    size_t total_size = sizeof(GcHeader) + size;
    void *block = ms_heap_alloc(total_size);
    
    if (!block) {
        // Heap full, try collecting
        ms_collect();
        block = ms_heap_alloc(total_size);
        if (!block) {
            fprintf(stderr, "GC: Out of memory (Mark-Sweep heap exhausted)\n");
            exit(1);
        }
    }

    // Capture actual block size from the free list node before overwriting it
    size_t actual_block_size = ((FreeHeader*)block)->size;

    GcHeader *header = (GcHeader *)block;
    header->prev = NULL;
    header->next = gc_objects;
    if (gc_objects) gc_objects->prev = header;
    gc_objects = header;
    
    header->size = size;
    header->block_size = actual_block_size; // Store actual block size for freeing
    header->marked = 0;
    header->trace = NULL;
    header->tag = GC_TAG_UNKNOWN;
    
    void *payload = (void *)(header + 1);
    memset(payload, 0, size);
    
    gc_bytes_allocated += size;
    internal_stats.allocated_bytes += size;
    internal_stats.current_bytes += size;
    internal_stats.wasted_bytes += (header->block_size - size); // Header + Padding

    // Trigger collection if threshold exceeded (though allocator handles OOM)
    if (!gc_collecting && gc_bytes_allocated > gc_next_threshold)
    {
        ms_collect();
        // Adjust threshold but keep within heap bounds logic
        size_t next = (size_t)(gc_bytes_allocated * GC_GROWTH_FACTOR);
        if (next > heap_size) next = heap_size; 
        gc_next_threshold = next;
    }
    return payload;
}

static void ms_set_trace(void *ptr, gc_trace_func trace)
{
    if (!ptr) return;
    GcHeader *header = gc_header_for(ptr);
    header->trace = trace;
}

static void ms_set_tag(void *ptr, unsigned char tag)
{
    if (!ptr) return;
    GcHeader *header = gc_header_for(ptr);
    header->tag = tag;
}

static void *ms_mark_ptr(void *ptr)
{
    if (!ptr) return NULL;
    GcHeader *header = gc_find_header(ptr);
    if (!header) return NULL;
    if (!header->marked)
    {
        header->marked = 1;
        if (header->trace) header->trace(ptr);
    }
    return ptr;
}

static void ms_add_root(void **slot)
{
    if (!slot) return;
    for (size_t i = 0; i < gc_root_count; ++i) {
        if (gc_roots[i].slot == slot) return;
    }
    gc_roots_reserve(gc_root_count + 1);
    gc_roots[gc_root_count++].slot = slot;
}

static void ms_remove_root(void **slot)
{
    if (!slot) return;
    for (size_t i = 0; i < gc_root_count; ++i) {
        if (gc_roots[i].slot == slot) {
            gc_roots[i] = gc_roots[gc_root_count - 1];
            gc_root_count--;
            return;
        }
    }
}

static void gc_mark_roots(void)
{
    for (size_t i = 0; i < gc_root_count; ++i) {
        void *ptr = *(gc_roots[i].slot);
        if (ptr) ms_mark_ptr(ptr);
    }
}

static void gc_sweep(void)
{
    GcHeader *obj = gc_objects;
    size_t scanned = 0;
    size_t survived = 0;
    
    while (obj)
    {
        GcHeader *next = obj->next;
        scanned++;
        if (!obj->marked)
        {
            // Unlink from object list
            if (obj->prev) obj->prev->next = obj->next;
            else gc_objects = obj->next;
            if (obj->next) obj->next->prev = obj->prev;

            // Update stats
            internal_stats.freed_bytes += obj->size;
            internal_stats.current_bytes -= obj->size;
            internal_stats.wasted_bytes -= (obj->block_size - obj->size);
            
            // Return to free list
            ms_heap_free(obj, obj->block_size);
        }
        else
        {
            obj->marked = 0;
            survived++;
        }
        obj = next;
    }
    internal_stats.objects_scanned += scanned;
    if (scanned > 0) {
        internal_stats.survival_rate = (double)survived / (double)scanned;
    }
}

static void ms_write_barrier(void *owner, void **slot, void *child)
{
    (void)owner; (void)slot; (void)child;
}

static void ms_collect(void)
{
    if (!gc_initialized || gc_collecting) return;
    gc_collecting = 1;
    
    double start_time = gc_get_time_ms();
    
    internal_stats.collections++;
    gc_mark_roots();
    gc_sweep();
    
    // Update metadata bytes
    size_t live_objects = 0;
    for (GcHeader *obj = gc_objects; obj; obj = obj->next) live_objects++;
    internal_stats.metadata_bytes = live_objects * sizeof(GcHeader);
    
    double elapsed = gc_get_time_ms() - start_time;
    internal_stats.last_gc_pause_ms = elapsed;
    internal_stats.total_gc_time_ms += elapsed;
    if (elapsed > internal_stats.max_gc_pause_ms) internal_stats.max_gc_pause_ms = elapsed;
    internal_stats.avg_gc_pause_ms = internal_stats.total_gc_time_ms / internal_stats.collections;
    
    gc_collecting = 0;
}

static void ms_free(void *ptr)
{
    if (!ptr) return;
    GcHeader *header = gc_header_for(ptr);
    
    if (header->prev) header->prev->next = header->next;
    else gc_objects = header->next;
    if (header->next) header->next->prev = header->prev;

    internal_stats.freed_bytes += header->size;
    internal_stats.current_bytes -= header->size;
    internal_stats.wasted_bytes -= (header->block_size - header->size);
    
    ms_heap_free(header, header->block_size);
}

static void ms_set_threshold(size_t bytes)
{
    if (bytes < 1024) bytes = 1024;
    gc_next_threshold = bytes;
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
        
        // Internal Fragmentation
        size_t total_allocated = internal_stats.current_bytes + internal_stats.wasted_bytes;
        if (total_allocated > 0) {
            out_stats->internal_fragmentation_ratio = (double)internal_stats.wasted_bytes / (double)total_allocated;
        } else {
            out_stats->internal_fragmentation_ratio = 0.0;
        }
        
        size_t live_objects = 0;
        for (GcHeader *obj = gc_objects; obj; obj = obj->next) live_objects++;
        if (live_objects > 0) {
            out_stats->average_padding_per_object = (double)internal_stats.wasted_bytes / (double)live_objects;
        } else {
            out_stats->average_padding_per_object = 0.0;
        }
        
        // External Fragmentation (Walk the Free List)
        size_t largest_free = 0;
        size_t total_free = 0;
        size_t free_blocks = 0;
        
        FreeHeader *curr = free_list;
        while (curr) {
            total_free += curr->size;
            if (curr->size > largest_free) largest_free = curr->size;
            free_blocks++;
            curr = curr->next;
        }
        
        out_stats->largest_free_block = largest_free;
        out_stats->total_free_memory = total_free;
        out_stats->free_blocks_count = free_blocks;
        
        if (free_blocks > 0) {
            out_stats->average_free_block_size = (double)total_free / (double)free_blocks;
        } else {
            out_stats->average_free_block_size = 0.0;
        }
        
        if (total_free > 0) {
            out_stats->fragmentation_index = 1.0 - ((double)largest_free / (double)total_free);
        } else {
            out_stats->fragmentation_index = 0.0;
        }
        
        // Track peak fragmentation
        if (out_stats->fragmentation_index > internal_stats.peak_fragmentation_index) {
            internal_stats.peak_fragmentation_index = out_stats->fragmentation_index;
        }
        out_stats->peak_fragmentation_index = internal_stats.peak_fragmentation_index;
    }
}

static double ms_get_collections_count(void) { return (double)internal_stats.collections; }
static double ms_get_allocated_bytes(void) { return (double)internal_stats.allocated_bytes; }
static double ms_get_freed_bytes(void) { return (double)internal_stats.freed_bytes; }
static double ms_get_current_bytes(void) { return (double)internal_stats.current_bytes; }

static size_t ms_heap_snapshot(GcObjectInfo *out, size_t capacity)
{
    size_t count = 0;
    GcHeader *obj = gc_objects;
    while (obj && count < capacity)
    {
        out[count].addr = (uintptr_t)(obj + 1);
        out[count].size = obj->size;
        out[count].generation = GC_GEN_OLD;
        out[count].tag = obj->tag;
        count++;
        obj = obj->next;
    }
    return count;
}

const GcBackend *gc_mark_sweep_backend(void)
{
    static const GcBackend backend = {
        ms_init,
        ms_allocate,
        ms_set_trace,
        ms_mark_ptr,
        ms_set_tag,
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
        ms_get_current_bytes,
        ms_heap_snapshot};
    return &backend;
}
