#ifndef GC_BACKEND_H
#define GC_BACKEND_H

#include "gc.h"

// Timing utilities for GC performance measurement
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static inline double gc_get_time_ms(void) {
    return emscripten_get_now();
}
#else
#include <time.h>
static inline double gc_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
#endif

typedef struct GcBackend {
    void (*init)(void);
    void *(*allocate)(size_t size);
    void (*set_trace)(void *ptr, gc_trace_func trace);
    void *(*mark_ptr)(void *ptr);
    void (*set_tag)(void *ptr, unsigned char tag);
    void (*add_root)(void **slot);
    void (*remove_root)(void **slot);
    void (*write_barrier)(void *owner, void **slot, void *child);
    void (*collect)(void);
    void (*free)(void *ptr);
    void (*set_threshold)(size_t bytes);
    size_t (*get_threshold)(void);
    void (*get_stats)(GcStats *out_stats);
    double (*get_collections_count)(void);
    double (*get_allocated_bytes)(void);
    double (*get_freed_bytes)(void);
    double (*get_current_bytes)(void);
    size_t (*heap_snapshot)(GcObjectInfo *out, size_t capacity);
} GcBackend;

const GcBackend *gc_mark_sweep_backend(void);
const GcBackend *gc_copying_backend(void);
const GcBackend *gc_generational_backend(void);

#endif
