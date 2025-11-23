#ifndef GC_BACKEND_H
#define GC_BACKEND_H

#include "gc.h"

typedef struct GcBackend {
    void (*init)(void);
    void *(*allocate)(size_t size);
    void (*set_trace)(void *ptr, gc_trace_func trace);
    void *(*mark_ptr)(void *ptr);
    void (*add_root)(void **slot);
    void (*remove_root)(void **slot);
    void (*write_barrier)(void *owner, void *child);
    void (*collect)(void);
    void (*free)(void *ptr);
    void (*set_threshold)(size_t bytes);
    size_t (*get_threshold)(void);
    void (*get_stats)(GcStats *out_stats);
    double (*get_collections_count)(void);
    double (*get_allocated_bytes)(void);
    double (*get_freed_bytes)(void);
    double (*get_current_bytes)(void);
} GcBackend;

const GcBackend *gc_mark_sweep_backend(void);
const GcBackend *gc_copying_backend(void);

#endif
