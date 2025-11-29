// generational.c - generational GC backend
#include "gc_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Nursery (young generation) size in bytes. Survivors that reach PROMOTE_AGE
// are copied into the old generation managed by mark-sweep.
#define DEFAULT_NURSERY_SIZE (512 * 1024)
#define PROMOTE_AGE 2
#define OLD_GROWTH_FACTOR 2.0

// Headers stored in the nursery (copying semi-space).
typedef struct NurseryHeader {
    size_t size;
    gc_trace_func trace;
    void *forward;
    unsigned char age;
    unsigned char tag;
} NurseryHeader;

// Headers stored in the old generation (mark-sweep list).
typedef struct OldHeader {
    struct OldHeader *prev;
    struct OldHeader *next;
    size_t size;
    size_t block_size; // Total size of the block (header + payload + padding)
    int marked;
    gc_trace_func trace;
    unsigned char tag;
} OldHeader;

typedef struct {
    void **slot;
} RootSlot;

// Hash table for fast root lookups
typedef struct {
    void **slot;
    size_t index; // Index in roots array
} RootHashEntry;

static RootHashEntry *root_hash = NULL;
static size_t root_hash_capacity = 0;
static size_t root_hash_count = 0;

// Hash function
static size_t hash_ptr(void *ptr) {
    size_t h = (size_t)ptr >> 3;
    return h ^ (h >> 16);
}

static void root_hash_insert(void **slot, size_t index);

static void root_hash_resize(size_t new_capacity) {
    RootHashEntry *old_hash = root_hash;
    size_t old_capacity = root_hash_capacity;

    root_hash_capacity = new_capacity;
    root_hash = (RootHashEntry *)calloc(root_hash_capacity, sizeof(RootHashEntry));
    root_hash_count = 0;

    if (old_hash) {
        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_hash[i].slot) {
                root_hash_insert(old_hash[i].slot, old_hash[i].index);
            }
        }
        free(old_hash);
    }
}

static void root_hash_insert(void **slot, size_t index) {
    if (root_hash_count * 2 >= root_hash_capacity) {
        root_hash_resize(root_hash_capacity ? root_hash_capacity * 2 : 1024);
    }
    
    size_t h = hash_ptr(slot) & (root_hash_capacity - 1);
    while (root_hash[h].slot) {
        if (root_hash[h].slot == slot) {
            root_hash[h].index = index; // Update index
            return;
        }
        h = (h + 1) & (root_hash_capacity - 1);
    }
    root_hash[h].slot = slot;
    root_hash[h].index = index;
    root_hash_count++;
}

static size_t root_hash_find(void **slot, int *found) {
    if (!root_hash) {
        *found = 0;
        return 0;
    }
    size_t h = hash_ptr(slot) & (root_hash_capacity - 1);
    while (root_hash[h].slot) {
        if (root_hash[h].slot == slot) {
            *found = 1;
            return root_hash[h].index;
        }
        h = (h + 1) & (root_hash_capacity - 1);
    }
    *found = 0;
    return 0;
}

static void root_hash_delete(void **slot) {
    if (!root_hash) return;
    size_t h = hash_ptr(slot) & (root_hash_capacity - 1);
    while (root_hash[h].slot) {
        if (root_hash[h].slot == slot) {
            root_hash[h].slot = NULL;
            root_hash_count--;
            
            // Rehash subsequent entries in the cluster
            size_t i = (h + 1) & (root_hash_capacity - 1);
            while (root_hash[i].slot) {
                void **s = root_hash[i].slot;
                size_t idx = root_hash[i].index;
                root_hash[i].slot = NULL;
                root_hash_count--;
                root_hash_insert(s, idx);
                i = (i + 1) & (root_hash_capacity - 1);
            }
            return;
        }
        h = (h + 1) & (root_hash_capacity - 1);
    }
}

typedef struct {
    void **slot;
} RememberedSlot;

// Global state --------------------------------------------------------------

static unsigned char *nursery_active = NULL;
static unsigned char *nursery_inactive = NULL;
static unsigned char *nursery_alloc = NULL;
static unsigned char *nursery_end = NULL;
static size_t nursery_size = DEFAULT_NURSERY_SIZE;
static int generational_initialized = 0;
static int minor_collecting = 0;
static int major_collecting = 0;

static RootSlot *roots = NULL;
static size_t root_count = 0;
static size_t root_capacity = 0;

static RememberedSlot *remembered = NULL;
static size_t remembered_count = 0;
static size_t remembered_capacity = 0;

static OldHeader *old_objects = NULL;
static size_t old_bytes_allocated = 0;
static size_t old_next_threshold = DEFAULT_NURSERY_SIZE * 2;
static GcStats gc_stats = {0, 0, 0, 0};

static size_t align_size(size_t size) {
    size_t align = sizeof(void*);
    return (size + align - 1) & ~(align - 1);
}

static int pointer_in_space(unsigned char *space, void *ptr) {
    return ptr && (unsigned char*)ptr >= space && (unsigned char*)ptr < space + nursery_size;
}

static void ensure_root_capacity(size_t needed) {
    if (root_capacity >= needed) return;
    size_t new_cap = root_capacity ? root_capacity * 2 : 32;
    while (new_cap < needed) new_cap *= 2;
    RootSlot *new_roots = (RootSlot*)realloc(roots, new_cap * sizeof(RootSlot));
    if (!new_roots) {
        fprintf(stderr, "Generational GC: failed to grow root set\n");
        exit(1);
    }
    roots = new_roots;
    root_capacity = new_cap;
}

static void ensure_remembered_capacity(size_t needed) {
    if (remembered_capacity >= needed) return;
    size_t new_cap = remembered_capacity ? remembered_capacity * 2 : 32;
    while (new_cap < needed) new_cap *= 2;
    RememberedSlot *new_slots = (RememberedSlot*)realloc(remembered, new_cap * sizeof(RememberedSlot));
    if (!new_slots) {
        fprintf(stderr, "Generational GC: failed to grow remembered set\n");
        exit(1);
    }
    remembered = new_slots;
    remembered_capacity = new_cap;
}

// Free List Allocator for Old Generation
#define ALIGNMENT sizeof(void*)
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define MIN_BLOCK_SIZE (sizeof(FreeHeader))

typedef struct FreeHeader {
    size_t size;
    struct FreeHeader *next;
} FreeHeader;

static uint8_t *old_heap_start = NULL;
static size_t old_heap_size = 0;
static FreeHeader *old_free_list = NULL;

static void old_heap_init(size_t size) {
    old_heap_size = size;
    old_heap_start = (uint8_t*)malloc(old_heap_size);
    if (!old_heap_start) {
        fprintf(stderr, "Generational GC: failed to allocate old generation heap (%zu bytes)\n", old_heap_size);
        exit(1);
    }
    old_free_list = (FreeHeader*)old_heap_start;
    old_free_list->size = old_heap_size;
    old_free_list->next = NULL;
}

static void *old_heap_alloc(size_t size) {
    size_t needed = ALIGN(size);
    if (needed < MIN_BLOCK_SIZE) needed = MIN_BLOCK_SIZE;
    
    FreeHeader *prev = NULL;
    FreeHeader *curr = old_free_list;
    
    while (curr) {
        if (curr->size >= needed) {
            if (curr->size >= needed + MIN_BLOCK_SIZE) {
                FreeHeader *remaining = (FreeHeader*)((uint8_t*)curr + needed);
                remaining->size = curr->size - needed;
                remaining->next = curr->next;
                if (prev) prev->next = remaining;
                else old_free_list = remaining;
                curr->size = needed;
            } else {
                if (prev) prev->next = curr->next;
                else old_free_list = curr->next;
            }
            return (void*)curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

static void old_heap_free(void *ptr, size_t size) {
    if (!ptr) return;
    FreeHeader *block = (FreeHeader*)ptr;
    block->size = size;
    
    FreeHeader *prev = NULL;
    FreeHeader *curr = old_free_list;
    
    while (curr && curr < block) {
        prev = curr;
        curr = curr->next;
    }
    
    block->next = curr;
    if (prev) prev->next = block;
    else old_free_list = block;
    
    if (curr && (uint8_t*)block + block->size == (uint8_t*)curr) {
        block->size += curr->size;
        block->next = curr->next;
    }
    if (prev && (uint8_t*)prev + prev->size == (uint8_t*)block) {
        prev->size += block->size;
        prev->next = block->next;
    }
}

static void major_collect(void);

// Old generation helpers (mark-sweep like)
static OldHeader *old_find_header(void *ptr) {
    if (!ptr) return NULL;
    // Simple bounds check
    if ((uint8_t*)ptr < old_heap_start || (uint8_t*)ptr >= old_heap_start + old_heap_size) return NULL;
    return ((OldHeader*)ptr) - 1;
}

static void *old_allocate(size_t size, gc_trace_func trace) {
    if (!old_heap_start) {
        // Initialize old gen heap (default 4MB if not set)
        size_t initial = gc_get_initial_heap_size();
        if (initial == 0) initial = 4 * 1024 * 1024;
        old_heap_init(initial);
    }

    size_t total_size = sizeof(OldHeader) + size;
    void *block = old_heap_alloc(total_size);
    
    if (!block) {
        // Try collecting major
        major_collect();
        block = old_heap_alloc(total_size);
        if (!block) {
            fprintf(stderr, "Generational GC: old-generation allocation failed (OOM)\n");
            exit(1);
        }
    }

    // Capture actual block size from the free list node before overwriting it
    size_t actual_block_size = ((FreeHeader*)block)->size;

    OldHeader *header = (OldHeader*)block;
    header->prev = NULL;
    header->next = old_objects;
    if (old_objects) old_objects->prev = header;
    old_objects = header;
    
    header->size = size;
    header->block_size = actual_block_size;
    
    header->marked = 0;
    header->trace = trace;
    void *payload = (void*)(header + 1);
    memset(payload, 0, size);
    header->tag = GC_TAG_UNKNOWN;
    
    old_bytes_allocated += size;
    // Track wasted bytes for internal fragmentation
    // We need a global tracker for this? Or calculate on demand?
    // Mark-Sweep tracked it incrementally. Let's do that.
    // But I need to add 'wasted_bytes' to global state or just use gc_stats.wasted_bytes?
    // gc_stats is reset on collection? No, it's cumulative?
    // Mark-Sweep uses internal_stats.
    // Generational uses gc_stats.
    // Let's update gc_stats.wasted_bytes here.
    // But wait, gen_get_stats recalculates it.
    // So I don't need to track it incrementally if I recalculate it.
    
    return payload;
}

static void old_remove(OldHeader *header) {
    if (header->prev) header->prev->next = header->next;
    else old_objects = header->next;
    if (header->next) header->next->prev = header->prev;
    
    old_bytes_allocated -= header->size;
    gc_stats.freed_bytes += header->size;
    
    old_heap_free(header, header->block_size);
}

static void old_mark(void *ptr);

// Nursery helpers
static NurseryHeader *nursery_header_for(void *ptr) {
    if (!ptr) return NULL;
    return ((NurseryHeader*)ptr) - 1;
}

static void swap_nursery_spaces(void) {
    unsigned char *tmp = nursery_inactive;
    nursery_inactive = nursery_active;
    nursery_active = tmp;
    nursery_alloc = nursery_active;
    nursery_end = nursery_active + nursery_size;
}

static void generational_init(void) {
    if (generational_initialized) return;
    size_t configured_size = gc_get_initial_heap_size();
    if (configured_size > 0) {
        nursery_size = align_size(configured_size);
    }
    nursery_active = (unsigned char*)malloc(nursery_size);
    nursery_inactive = (unsigned char*)malloc(nursery_size);
    if (!nursery_active || !nursery_inactive) {
        fprintf(stderr, "Generational GC: failed to allocate nursery (%zu bytes)\n", nursery_size);
        exit(1);
    }
    nursery_alloc = nursery_active;
    nursery_end = nursery_active + nursery_size;
    root_count = root_capacity = 0;
    remembered_count = remembered_capacity = 0;
    free(roots); roots = NULL;
    
    if (root_hash) {
        free(root_hash);
        root_hash = NULL;
        root_hash_capacity = 0;
        root_hash_count = 0;
    }
    free(remembered); remembered = NULL;
    memset(&gc_stats, 0, sizeof(gc_stats));
    // Timing fields are zero-initialized by memset
    old_objects = NULL;
    old_bytes_allocated = 0;
    old_next_threshold = nursery_size * 2;
    generational_initialized = 1;
}

// Promotion stack for iterative deep promotion
static void **promotion_stack = NULL;
static size_t promotion_sp = 0;
static size_t promotion_cap = 0;
static int tracing_promoted = 0;

static void push_promotion(void *obj) {
    if (promotion_sp >= promotion_cap) {
        size_t new_cap = promotion_cap ? promotion_cap * 2 : 1024;
        void **new_stack = (void**)realloc(promotion_stack, new_cap * sizeof(void*));
        if (!new_stack) {
            fprintf(stderr, "Generational GC: failed to grow promotion stack\n");
            exit(1);
        }
        promotion_stack = new_stack;
        promotion_cap = new_cap;
    }
    promotion_stack[promotion_sp++] = obj;
}

static void *promote_object(NurseryHeader *header, void *payload) {
    void *old_obj = old_allocate(header->size, header->trace);
    memcpy(old_obj, payload, header->size);
    OldHeader *old_header = old_find_header(old_obj);
    if (old_header) old_header->tag = header->tag;
    
    header->forward = old_obj;
    gc_stats.objects_promoted++;
    
    // Push to stack for deferred tracing (iterative deep promotion)
    if (header->trace) {
        push_promotion(old_obj);
    }
    
    return old_obj;
}

static void *copy_young_object(void *ptr) {
    if (!ptr) return NULL;
    if (!pointer_in_space(nursery_inactive, ptr)) {
        return ptr;
    }
    NurseryHeader *old_header = nursery_header_for(ptr);
    if (old_header->forward) {
        return old_header->forward;
    }
    
    // Promote if age threshold reached OR if we are tracing a promoted object (Deep Promotion)
    if (tracing_promoted || old_header->age + 1 >= PROMOTE_AGE) {
        return promote_object(old_header, ptr);
    }
    
    size_t total = sizeof(NurseryHeader) + old_header->size;
    if (nursery_alloc + total > nursery_end) {
        return promote_object(old_header, ptr);
    }
    
    NurseryHeader *new_header = (NurseryHeader*)nursery_alloc;
    nursery_alloc += total;
    new_header->size = old_header->size;
    new_header->trace = old_header->trace;
    new_header->forward = NULL;
    new_header->age = old_header->age + 1;
    new_header->tag = old_header->tag;
    void *payload = (void*)(new_header + 1);
    memcpy(payload, old_header + 1, old_header->size);
    old_header->forward = payload;
    gc_stats.objects_copied++;
    return payload;
}

static void remembered_cleanup(void) {
    size_t write = 0;
    for (size_t i = 0; i < remembered_count; ++i) {
        void **slot = remembered[i].slot;
        void *value = slot ? *slot : NULL;
        if (value && pointer_in_space(nursery_active, value)) {
            remembered[write++] = remembered[i];
        }
    }
    remembered_count = write;
}

static void trace_roots_for_minor(void);
static void scan_nursery(void);
static void *gen_mark_ptr(void *ptr);
static void major_collect(void);

static void minor_collect(void) {
    if (!generational_initialized || minor_collecting) return;
    minor_collecting = 1;
    
    double start_time = gc_get_time_ms();
    size_t objects_before = gc_stats.objects_copied + gc_stats.objects_promoted;
    
    gc_stats.collections++;
    swap_nursery_spaces();
    
    // Reset promotion stack
    promotion_sp = 0;
    
    trace_roots_for_minor();
    
    // Interleaved scanning of nursery (survivors) and promotion stack (promoted objects)
    unsigned char *scan = nursery_active;
    
    while (1) {
        int work_done = 0;
        
        // Scan nursery survivors
        while (scan < nursery_alloc) {
            work_done = 1;
            NurseryHeader *header = (NurseryHeader*)scan;
            void *payload = (void*)(header + 1);
            
            // Tracing survivors: children stay in nursery (unless age > limit)
            tracing_promoted = 0; 
            if (header->trace) header->trace(payload);
            
            scan += sizeof(NurseryHeader) + header->size;
        }
        
        // Process promotion stack
        while (promotion_sp > 0) {
            work_done = 1;
            void *obj = promotion_stack[--promotion_sp];
            OldHeader *header = old_find_header(obj);
            
            // Tracing promoted objects: children MUST be promoted (Deep Promotion)
            tracing_promoted = 1;
            if (header && header->trace) header->trace(obj);
        }
        
        if (!work_done) break;
    }
    tracing_promoted = 0;
    
    scan_nursery(); // Re-scan? No, we already scanned in the loop.
                    // But scan_nursery function updates stats. Let's extract stats update.
    
    // Update scanned count
    // We need to count how many objects we scanned.
    // The loop above scanned everything.
    // Let's just iterate nursery to count scanned objects for stats.
    size_t scanned = 0;
    unsigned char *stat_scan = nursery_active;
    while (stat_scan < nursery_alloc) {
        NurseryHeader *header = (NurseryHeader*)stat_scan;
        scanned++;
        stat_scan += sizeof(NurseryHeader) + header->size;
    }
    gc_stats.objects_scanned += scanned;
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;

    remembered_cleanup();
    
    size_t objects_after = gc_stats.objects_copied + gc_stats.objects_promoted;
    size_t survived_this_cycle = objects_after - objects_before;
    if (gc_stats.objects_scanned > 0) {
        gc_stats.survival_rate = (double)survived_this_cycle / (double)gc_stats.objects_scanned;
    }
    
    // Metadata overhead
    size_t nursery_objects = scanned;
    size_t old_gen_count = 0;
    for (OldHeader *obj = old_objects; obj; obj = obj->next) old_gen_count++;
    
    gc_stats.metadata_bytes = (nursery_objects * sizeof(NurseryHeader)) + 
                               (old_gen_count * sizeof(OldHeader));
    
    double elapsed = gc_get_time_ms() - start_time;
    gc_stats.last_gc_pause_ms = elapsed;
    gc_stats.total_gc_time_ms += elapsed;
    if (elapsed > gc_stats.max_gc_pause_ms) gc_stats.max_gc_pause_ms = elapsed;
    gc_stats.avg_gc_pause_ms = gc_stats.total_gc_time_ms / gc_stats.collections;
    
    minor_collecting = 0;
}

static void *gen_allocate(size_t size) {
    if (!generational_initialized) generational_init();
    size_t payload = align_size(size);
    size_t total = sizeof(NurseryHeader) + payload;
    if (nursery_alloc + total > nursery_end) {
        minor_collect();
        if (nursery_alloc + total > nursery_end) {
            major_collect();
            if (nursery_alloc + total > nursery_end) {
                fprintf(stderr, "Generational GC: out of memory allocating %zu bytes\n", size);
                exit(1);
            }
        }
    }
    NurseryHeader *header = (NurseryHeader*)nursery_alloc;
    nursery_alloc += total;
    header->size = payload;
    header->trace = NULL;
    header->forward = NULL;
    header->age = 0;
    header->tag = GC_TAG_UNKNOWN;
    void *payload_ptr = (void*)(header + 1);
    memset(payload_ptr, 0, payload);
    gc_stats.allocated_bytes += size;
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;
    return payload_ptr;
}

static void gen_set_trace(void *ptr, gc_trace_func trace) {
    if (!ptr) return;
    if (!generational_initialized) generational_init();
    if (pointer_in_space(nursery_active, ptr) || pointer_in_space(nursery_inactive, ptr)) {
        NurseryHeader *header = nursery_header_for(ptr);
        header->trace = trace;
    } else {
        OldHeader *header = old_find_header(ptr);
        if (header) header->trace = trace;
    }
}

static void gen_set_tag(void *ptr, unsigned char tag) {
    if (!ptr) return;
    if (!generational_initialized) generational_init();
    if (pointer_in_space(nursery_active, ptr) || pointer_in_space(nursery_inactive, ptr)) {
        NurseryHeader *header = nursery_header_for(ptr);
        if (header) header->tag = tag;
    } else {
        OldHeader *header = old_find_header(ptr);
        if (header) header->tag = tag;
    }
}

static void ensure_root_slot(void **slot) {
    if (!slot) return;
    
    int found = 0;
    root_hash_find(slot, &found);
    if (found) return;

    ensure_root_capacity(root_count + 1);
    roots[root_count].slot = slot;
    root_hash_insert(slot, root_count);
    root_count++;
}

static void remove_root_slot(void **slot) {
    if (!slot) return;
    
    int found = 0;
    size_t index = root_hash_find(slot, &found);
    if (!found) return;

    // Remove from hash
    root_hash_delete(slot);

    // Swap with last element in array
    size_t last_index = root_count - 1;
    if (index != last_index) {
        RootSlot last = roots[last_index];
        roots[index] = last;
        
        // Update index of moved element in hash
        root_hash_insert(last.slot, index);
    }
    
    root_count--;
}

static void add_remembered_slot(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < remembered_count; ++i) {
        if (remembered[i].slot == slot) return;
    }
    ensure_remembered_capacity(remembered_count + 1);
    remembered[remembered_count++].slot = slot;
}

static void trace_roots_for_minor(void) {
    for (size_t i = 0; i < root_count; ++i) {
        void **slot = roots[i].slot;
        if (slot && *slot) {
            *slot = gen_mark_ptr(*slot);
        }
    }
    for (size_t i = 0; i < remembered_count; ++i) {
        void **slot = remembered[i].slot;
        if (slot && *slot) {
            *slot = gen_mark_ptr(*slot);
        }
    }
}

static void trace_roots_for_major(void) {
    for (size_t i = 0; i < root_count; ++i) {
        void **slot = roots[i].slot;
        if (slot && *slot) {
            gen_mark_ptr(*slot);
        }
    }
}

static void scan_nursery(void) {
    unsigned char *scan = nursery_active;
    size_t scanned = 0;
    while (scan < nursery_alloc) {
        NurseryHeader *header = (NurseryHeader*)scan;
        void *payload = (void*)(header + 1);
        scanned++;
        if (header->trace) header->trace(payload);
        memcpy(scan, scan, 0); // suppress unused warning
        scan += sizeof(NurseryHeader) + header->size;
    }
    gc_stats.objects_scanned += scanned;
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;
}

static void mark_old_roots(void);
static void sweep_old(void);

static void major_collect(void) {
    if (major_collecting) return;
    
    // Note: minor_collect() already updates timing stats
    // We only need to track that major collection is happening
    minor_collect();
    major_collecting = 1;
    mark_old_roots();
    sweep_old();
    major_collecting = 0;
}

static void mark_old_roots(void) {
    trace_roots_for_major();
    for (size_t i = 0; i < remembered_count; ++i) {
        void **slot = remembered[i].slot;
        if (slot && *slot) {
            gen_mark_ptr(*slot);
        }
    }
}

static void sweep_old(void) {
    OldHeader *obj = old_objects;
    while (obj) {
        OldHeader *next = obj->next;
        if (!obj->marked) {
            old_remove(obj);
        } else {
            obj->marked = 0;
        }
        obj = next;
    }
    old_next_threshold = (size_t)(old_bytes_allocated * OLD_GROWTH_FACTOR + 1024);
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;
}

static void gen_add_root(void **slot) {
    ensure_root_slot(slot);
}

static void gen_remove_root(void **slot) {
    remove_root_slot(slot);
}

static void gen_write_barrier(void *owner, void **slot, void *child) {
    (void)owner;
    if (!slot || !child) return;
    if (pointer_in_space(nursery_active, child)) {
        add_remembered_slot(slot);
    }
}

static void gen_collect(void) {
    minor_collect();
    if (old_bytes_allocated > old_next_threshold) {
        major_collect();
    }
}

static void gen_free(void *ptr) {
    OldHeader *header = old_find_header(ptr);
    if (header) old_remove(header);
}

static void gen_set_threshold(size_t bytes) {
    if (bytes < 1024) bytes = 1024;
    old_next_threshold = bytes;
}

static size_t gen_get_threshold(void) {
    return old_next_threshold;
}

static void gen_get_stats(GcStats *out_stats) {
    if (!out_stats) return;
    *out_stats = gc_stats;

    // Calculate nursery fragmentation (External is 0 because it's contiguous)
    size_t nursery_free = 0;
    if (nursery_end && nursery_alloc) {
        nursery_free = nursery_end - nursery_alloc;
    }
    
    // Calculate old generation fragmentation (Free List)
    size_t old_largest_free = 0;
    size_t old_total_free = 0;
    size_t old_free_blocks = 0;
    
    FreeHeader *curr = old_free_list;
    while (curr) {
        old_total_free += curr->size;
        if (curr->size > old_largest_free) old_largest_free = curr->size;
        old_free_blocks++;
        curr = curr->next;
    }
    
    // Combine metrics
    size_t total_free = nursery_free + old_total_free;
    size_t largest_free = (nursery_free > old_largest_free) ? nursery_free : old_largest_free;
    
    out_stats->largest_free_block = largest_free;
    out_stats->total_free_memory = total_free;
    out_stats->free_blocks_count = old_free_blocks + ((nursery_free > 0) ? 1 : 0);
    
    if (out_stats->free_blocks_count > 0) {
        out_stats->average_free_block_size = (double)total_free / (double)out_stats->free_blocks_count;
    } else {
        out_stats->average_free_block_size = 0.0;
    }
    
    if (total_free > 0) {
        out_stats->fragmentation_index = 1.0 - ((double)largest_free / (double)total_free);
    } else {
        out_stats->fragmentation_index = 0.0;
    }

    // Calculate internal fragmentation (headers)
    size_t wasted = 0;
    size_t obj_count = 0;

    // Walk nursery
    if (nursery_active && nursery_alloc) {
        unsigned char *scan = nursery_active;
        while (scan < nursery_alloc) {
            NurseryHeader *h = (NurseryHeader*)scan;
            wasted += sizeof(NurseryHeader);
            obj_count++;
            scan += sizeof(NurseryHeader) + h->size;
        }
    }

    // Walk old generation
    for (OldHeader *h = old_objects; h; h = h->next) {
        // We need to account for alignment padding in the block
        size_t block_size = ALIGN(sizeof(OldHeader) + h->size);
        wasted += (block_size - h->size);
        obj_count++;
    }

    out_stats->wasted_bytes = wasted;
    
    // Internal fragmentation ratio
    // Total allocated memory (including headers/padding)
    size_t total_allocated_mem = out_stats->current_bytes + wasted; // current_bytes is payload only
    
    if (total_allocated_mem > 0) {
        out_stats->internal_fragmentation_ratio = (double)wasted / (double)total_allocated_mem;
    } else {
        out_stats->internal_fragmentation_ratio = 0.0;
    }
    
    if (obj_count > 0) {
        out_stats->average_padding_per_object = (double)wasted / (double)obj_count;
    } else {
        out_stats->average_padding_per_object = 0.0;
    }
    
    // Track peak fragmentation
    static double peak_frag = 0.0;
    if (out_stats->fragmentation_index > peak_frag) {
        peak_frag = out_stats->fragmentation_index;
    }
    out_stats->peak_fragmentation_index = peak_frag;
    out_stats->fragmentation_growth_rate = 0.0;
}

static double gen_get_collections_count(void) { return (double)gc_stats.collections; }
static double gen_get_allocated_bytes(void) { return (double)gc_stats.allocated_bytes; }
static double gen_get_freed_bytes(void) { return (double)gc_stats.freed_bytes; }
static double gen_get_current_bytes(void) { return (double)gc_stats.current_bytes; }

static size_t gen_heap_snapshot(GcObjectInfo *out, size_t capacity) {
    size_t count = 0;
    unsigned char *scan = nursery_active;
    while (scan < nursery_alloc && count < capacity) {
        NurseryHeader *header = (NurseryHeader*)scan;
        out[count].addr = (uintptr_t)(header + 1);
        out[count].size = header->size;
        out[count].generation = GC_GEN_NURSERY;
        out[count].tag = header->tag;
        count++;
        scan += sizeof(NurseryHeader) + header->size;
    }
    OldHeader *obj = old_objects;
    while (obj && count < capacity) {
        out[count].addr = (uintptr_t)(obj + 1);
        out[count].size = obj->size;
        out[count].generation = GC_GEN_OLD;
        out[count].tag = obj->tag;
        count++;
        obj = obj->next;
    }
    return count;
}

static void old_mark(void *ptr) {
    OldHeader *header = old_find_header(ptr);
    if (!header || header->marked) return;
    header->marked = 1;
    if (header->trace) header->trace(ptr);
}

static void *gen_mark_ptr(void *ptr) {
    if (!ptr) return NULL;
    if (minor_collecting) {
        return copy_young_object(ptr);
    }
    if (major_collecting) {
        old_mark(ptr);
    }
    return ptr;
}

const GcBackend *gc_generational_backend(void) {
    static const GcBackend backend = {
        generational_init,
        gen_allocate,
        gen_set_trace,
        gen_mark_ptr,
        gen_set_tag,
        gen_add_root,
        gen_remove_root,
        gen_write_barrier,
        gen_collect,
        gen_free,
        gen_set_threshold,
        gen_get_threshold,
        gen_get_stats,
        gen_get_collections_count,
        gen_get_allocated_bytes,
        gen_get_freed_bytes,
        gen_get_current_bytes,
        gen_heap_snapshot
    };
    return &backend;
}
