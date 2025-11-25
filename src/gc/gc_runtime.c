#include "gc.h"
#include "gc_backend.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static const GcBackend *gc_backend = NULL;
static char backend_override[32];
static int backend_override_set = 0;
static size_t initial_heap_size = 0;

void gc_set_initial_heap_size(size_t size) {
    initial_heap_size = size;
}

size_t gc_get_initial_heap_size(void) {
    if (initial_heap_size > 0) return initial_heap_size;
    const char *env = getenv("GC_INITIAL_HEAP_SIZE");
    if (env) {
        return (size_t)atol(env);
    }
    return 0;
}

static const GcBackend *select_backend(void) {
    const char *env = NULL;
    if (backend_override_set) {
        env = backend_override;
    } else {
        env = getenv("GC_BACKEND");
    }
    if (env) {
        if (strcmp(env, "copy") == 0 || strcmp(env, "copying") == 0 || strcmp(env, "semispace") == 0) {
            return gc_copying_backend();
        }
        if (strcmp(env, "gen") == 0 || strcmp(env, "generational") == 0) {
            return gc_generational_backend();
        }
    }
    return gc_mark_sweep_backend();
}

static void ensure_backend(void) {
    if (!gc_backend) {
        gc_backend = select_backend();
    }
}

void gc_init(void) {
    ensure_backend();
    gc_backend->init();
}

void *gc_allocate(size_t size) {
    ensure_backend();
    return gc_backend->allocate(size);
}

void gc_set_trace(void *ptr, gc_trace_func trace) {
    ensure_backend();
    gc_backend->set_trace(ptr, trace);
}

void *gc_mark_ptr(void *ptr) {
    ensure_backend();
    return gc_backend->mark_ptr(ptr);
}

void gc_set_tag(void *ptr, unsigned char tag) {
    ensure_backend();
    if (gc_backend->set_tag) gc_backend->set_tag(ptr, tag);
}

void gc_add_root(void **slot) {
    ensure_backend();
    gc_backend->add_root(slot);
}

void gc_remove_root(void **slot) {
    ensure_backend();
    gc_backend->remove_root(slot);
}

void gc_write_barrier(void *owner, void **slot, void *child) {
    ensure_backend();
    if (gc_backend->write_barrier) {
        gc_backend->write_barrier(owner, slot, child);
    }
}

void gc_collect(void) {
    ensure_backend();
    gc_backend->collect();
}

void gc_free(void *ptr) {
    ensure_backend();
    gc_backend->free(ptr);
}

void gc_set_threshold(size_t bytes) {
    ensure_backend();
    gc_backend->set_threshold(bytes);
}

size_t gc_get_threshold(void) {
    ensure_backend();
    return gc_backend->get_threshold();
}

void gc_get_stats(GcStats *out_stats) {
    ensure_backend();
    if (gc_backend->get_stats) {
        gc_backend->get_stats(out_stats);
    } else if (out_stats) {
        out_stats->collections = 0;
        out_stats->allocated_bytes = 0;
        out_stats->freed_bytes = 0;
        out_stats->current_bytes = 0;
    }
}

double gc_get_collections_count(void) {
    ensure_backend();
    return gc_backend->get_collections_count ? gc_backend->get_collections_count() : 0.0;
}

double gc_get_allocated_bytes(void) {
    ensure_backend();
    return gc_backend->get_allocated_bytes ? gc_backend->get_allocated_bytes() : 0.0;
}

double gc_get_freed_bytes(void) {
    ensure_backend();
    return gc_backend->get_freed_bytes ? gc_backend->get_freed_bytes() : 0.0;
}

double gc_get_current_bytes(void) {
    ensure_backend();
    return gc_backend->get_current_bytes ? gc_backend->get_current_bytes() : 0.0;
}

size_t gc_heap_snapshot(GcObjectInfo *out, size_t capacity) {
    ensure_backend();
    if (!gc_backend->heap_snapshot) return 0;
    return gc_backend->heap_snapshot(out, capacity);
}

size_t gc_heap_snapshot_flat(uint32_t *out, size_t capacity) {
    ensure_backend();
    if (!gc_backend->heap_snapshot || !out || capacity == 0) return 0;
    size_t entry_count = capacity;
    GcObjectInfo *buffer = (GcObjectInfo*)malloc(sizeof(GcObjectInfo) * entry_count);
    if (!buffer) return 0;
    size_t written = gc_backend->heap_snapshot(buffer, entry_count);
    for (size_t i = 0; i < written; ++i) {
        out[i * 4 + 0] = (uint32_t)buffer[i].addr;
        out[i * 4 + 1] = (uint32_t)buffer[i].size;
        out[i * 4 + 2] = (uint32_t)buffer[i].generation;
        out[i * 4 + 3] = (uint32_t)buffer[i].tag;
    }
    free(buffer);
    return written;
}

size_t gc_heap_snapshot_entry_size(void) {
    return sizeof(GcObjectInfo);
}

size_t gc_heap_snapshot_addr_offset(void) {
    return offsetof(GcObjectInfo, addr);
}

size_t gc_heap_snapshot_size_offset(void) {
    return offsetof(GcObjectInfo, size);
}

size_t gc_heap_snapshot_generation_offset(void) {
    return offsetof(GcObjectInfo, generation);
}

size_t gc_heap_snapshot_tag_offset(void) {
    return offsetof(GcObjectInfo, tag);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
void gc_set_backend_env(const char *name) {
    if (!name) return;
    strncpy(backend_override, name, sizeof(backend_override) - 1);
    backend_override[sizeof(backend_override) - 1] = '\0';
    backend_override_set = 1;
}
#endif
