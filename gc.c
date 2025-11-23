// gc.c - Simple mark-and-sweep garbage collector
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct GcHeader {
    struct GcHeader *prev;
    struct GcHeader *next;
    size_t size;
    int marked;
    gc_trace_func trace;
} GcHeader;

typedef struct {
    void **slot;
} GcRoot;

static GcHeader *gc_objects = NULL;
static size_t gc_bytes_allocated = 0;
static size_t gc_next_threshold = 1024 * 1024; // 1 MB default
static const double GC_GROWTH_FACTOR = 2.0;
static GcRoot *gc_roots = NULL;
static size_t gc_root_count = 0;
static size_t gc_root_capacity = 0;
static int gc_initialized = 0;
static int gc_collecting = 0;

static GcHeader *gc_header_for(void *ptr) {
    if (!ptr) return NULL;
    return ((GcHeader*)ptr) - 1;
}

static GcHeader *gc_find_header(void *ptr) {
    if (!ptr) return NULL;
    for (GcHeader *h = gc_objects; h; h = h->next) {
        if ((void*)(h + 1) == ptr) return h;
    }
    return NULL;
}

static void gc_roots_reserve(size_t capacity) {
    if (gc_root_capacity >= capacity) return;
    size_t new_cap = gc_root_capacity ? gc_root_capacity * 2 : 32;
    while (new_cap < capacity) new_cap *= 2;
    GcRoot *new_roots = (GcRoot*)realloc(gc_roots, new_cap * sizeof(GcRoot));
    if (!new_roots) {
        fprintf(stderr, "GC: failed to grow root set\n");
        exit(1);
    }
    gc_roots = new_roots;
    gc_root_capacity = new_cap;
}

void gc_init(void) {
    if (gc_initialized) return;
    gc_initialized = 1;
    gc_objects = NULL;
    gc_roots = NULL;
    gc_root_count = 0;
    gc_root_capacity = 0;
    gc_bytes_allocated = 0;
    gc_next_threshold = 1024 * 1024;
}

void *gc_allocate(size_t size) {
    if (!gc_initialized) {
        fprintf(stderr, "GC not initialized. Call gc_init() first.\n");
        exit(1);
    }
    GcHeader *header = (GcHeader*)malloc(sizeof(GcHeader) + size);
    if (!header) {
        fprintf(stderr, "GC allocation failed for %zu bytes\n", size);
        exit(1);
    }
    header->prev = NULL;
    header->next = gc_objects;
    if (gc_objects) gc_objects->prev = header;
    gc_objects = header;
    header->size = size;
    header->marked = 0;
    header->trace = NULL;
    void *payload = (void*)(header + 1);
    memset(payload, 0, size);
    gc_bytes_allocated += size;
    if (!gc_collecting && gc_bytes_allocated > gc_next_threshold) {
        gc_collect();
        gc_next_threshold = (size_t)(gc_bytes_allocated * GC_GROWTH_FACTOR);
    }
    return payload;
}

void gc_set_trace(void *ptr, gc_trace_func trace) {
    if (!ptr) return;
    GcHeader *header = gc_header_for(ptr);
    header->trace = trace;
}

void gc_mark_ptr(void *ptr) {
    if (!ptr) return;
    GcHeader *header = gc_find_header(ptr);
    if (!header || header->marked) return;
    header->marked = 1;
    if (header->trace) {
        header->trace(ptr);
    }
}

void gc_add_root(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < gc_root_count; ++i) {
        if (gc_roots[i].slot == slot) return;
    }
    gc_roots_reserve(gc_root_count + 1);
    gc_roots[gc_root_count++].slot = slot;
}

void gc_remove_root(void **slot) {
    if (!slot) return;
    for (size_t i = 0; i < gc_root_count; ++i) {
        if (gc_roots[i].slot == slot) {
            gc_roots[i] = gc_roots[gc_root_count - 1];
            gc_root_count--;
            return;
        }
    }
}

static void gc_mark_roots(void) {
    for (size_t i = 0; i < gc_root_count; ++i) {
        void *ptr = *(gc_roots[i].slot);
        if (ptr) gc_mark_ptr(ptr);
    }
}

static void gc_sweep(void) {
    GcHeader *obj = gc_objects;
    while (obj) {
        GcHeader *next = obj->next;
        if (!obj->marked) {
            if (obj->prev) obj->prev->next = obj->next;
            else gc_objects = obj->next;
            if (obj->next) obj->next->prev = obj->prev;
            free(obj);
        } else {
            obj->marked = 0;
        }
        obj = next;
    }
}

void gc_collect(void) {
    if (!gc_initialized || gc_collecting) return;
    gc_collecting = 1;
    gc_mark_roots();
    gc_sweep();
    gc_next_threshold = (size_t)(gc_bytes_allocated * GC_GROWTH_FACTOR + 1024);
    gc_collecting = 0;
}

void gc_free(void *ptr) {
    if (!ptr) return;
    GcHeader *header = gc_header_for(ptr);
    if (header->prev) header->prev->next = header->next;
    else gc_objects = header->next;
    if (header->next) header->next->prev = header->prev;
    free(header);
}

void gc_set_threshold(size_t bytes) {
    if (bytes < 1024) bytes = 1024;
    gc_next_threshold = bytes;
    if (gc_bytes_allocated > gc_next_threshold) {
        gc_collect();
    }
}

size_t gc_get_threshold(void) {
    return gc_next_threshold;
}
