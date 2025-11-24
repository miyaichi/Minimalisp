#include "gc_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
} NurseryHeader;

// Headers stored in the old generation (mark-sweep list).
typedef struct OldHeader {
    struct OldHeader *prev;
    struct OldHeader *next;
    size_t size;
    int marked;
    gc_trace_func trace;
} OldHeader;

typedef struct {
    void **slot;
} RootSlot;

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

// Old generation helpers (mark-sweep like)
static OldHeader *old_find_header(void *ptr) {
    if (!ptr) return NULL;
    OldHeader *h = old_objects;
    while (h) {
        if ((void*)(h + 1) == ptr) return h;
        h = h->next;
    }
    return NULL;
}

static void *old_allocate(size_t size, gc_trace_func trace) {
    OldHeader *header = (OldHeader*)malloc(sizeof(OldHeader) + size);
    if (!header) {
        fprintf(stderr, "Generational GC: old-generation allocation failed for %zu bytes\n", size);
        exit(1);
    }
    header->prev = NULL;
    header->next = old_objects;
    if (old_objects) old_objects->prev = header;
    old_objects = header;
    header->size = size;
    header->marked = 0;
    header->trace = trace;
    void *payload = (void*)(header + 1);
    memset(payload, 0, size);
    old_bytes_allocated += size;
    return payload;
}

static void old_remove(OldHeader *header) {
    if (header->prev) header->prev->next = header->next;
    else old_objects = header->next;
    if (header->next) header->next->prev = header->prev;
    old_bytes_allocated -= header->size;
    gc_stats.freed_bytes += header->size;
    free(header);
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
    free(remembered); remembered = NULL;
    memset(&gc_stats, 0, sizeof(gc_stats));
    old_objects = NULL;
    old_bytes_allocated = 0;
    old_next_threshold = nursery_size * 2;
    generational_initialized = 1;
}

static void *promote_object(NurseryHeader *header, void *payload) {
    void *old_obj = old_allocate(header->size, header->trace);
    memcpy(old_obj, payload, header->size);
    header->forward = old_obj;
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
    if (old_header->age + 1 >= PROMOTE_AGE) {
        return promote_object(old_header, ptr);
    }
    size_t total = sizeof(NurseryHeader) + old_header->size;
    if (nursery_alloc + total > nursery_end) {
        fprintf(stderr, "Generational GC: nursery overflow during copy\n");
        exit(1);
    }
    NurseryHeader *new_header = (NurseryHeader*)nursery_alloc;
    nursery_alloc += total;
    new_header->size = old_header->size;
    new_header->trace = old_header->trace;
    new_header->forward = NULL;
    new_header->age = old_header->age + 1;
    void *payload = (void*)(new_header + 1);
    memcpy(payload, old_header + 1, old_header->size);
    old_header->forward = payload;
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

static void minor_collect(void);
static void major_collect(void);

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
    void *payload_ptr = (void*)(header + 1);
    memset(payload_ptr, 0, payload);
    gc_stats.allocated_bytes += size;
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;
    return payload_ptr;
}

static void gen_set_trace(void *ptr, gc_trace_func trace) {
    if (!ptr) return;
    if (pointer_in_space(nursery_active, ptr) || pointer_in_space(nursery_inactive, ptr)) {
        NurseryHeader *header = nursery_header_for(ptr);
        header->trace = trace;
    } else {
        OldHeader *header = old_find_header(ptr);
        if (header) header->trace = trace;
    }
}

static void ensure_root_slot(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < root_count; ++i) {
        if (roots[i].slot == slot) return;
    }
    ensure_root_capacity(root_count + 1);
    roots[root_count++].slot = slot;
}

static void remove_root_slot(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < root_count; ++i) {
        if (roots[i].slot == slot) {
            roots[i] = roots[root_count - 1];
            root_count--;
            return;
        }
    }
}

static void add_remembered_slot(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < remembered_count; ++i) {
        if (remembered[i].slot == slot) return;
    }
    ensure_remembered_capacity(remembered_count + 1);
    remembered[remembered_count++].slot = slot;
}

static void *gen_mark_ptr(void *ptr);

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
    while (scan < nursery_alloc) {
        NurseryHeader *header = (NurseryHeader*)scan;
        void *payload = (void*)(header + 1);
        if (header->trace) header->trace(payload);
        memcpy(scan, scan, 0); // suppress unused warning
        scan += sizeof(NurseryHeader) + header->size;
    }
    gc_stats.current_bytes = (nursery_alloc - nursery_active) + old_bytes_allocated;
}

static void minor_collect(void) {
    if (!generational_initialized || minor_collecting) return;
    minor_collecting = 1;
    gc_stats.collections++;
    swap_nursery_spaces();
    trace_roots_for_minor();
    scan_nursery();
    remembered_cleanup();
    minor_collecting = 0;
}

static void mark_old_roots(void);
static void sweep_old(void);

static void major_collect(void) {
    if (major_collecting) return;
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
}

static double gen_get_collections_count(void) { return (double)gc_stats.collections; }
static double gen_get_allocated_bytes(void) { return (double)gc_stats.allocated_bytes; }
static double gen_get_freed_bytes(void) { return (double)gc_stats.freed_bytes; }
static double gen_get_current_bytes(void) { return (double)gc_stats.current_bytes; }

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
        gen_get_current_bytes
    };
    return &backend;
}
