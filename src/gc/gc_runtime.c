#include "gc.h"
#include "gc_backend.h"

static const GcBackend *gc_backend = NULL;

static void ensure_backend(void) {
    if (!gc_backend) {
        gc_backend = gc_mark_sweep_backend();
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

void gc_mark_ptr(void *ptr) {
    ensure_backend();
    gc_backend->mark_ptr(ptr);
}

void gc_add_root(void **slot) {
    ensure_backend();
    gc_backend->add_root(slot);
}

void gc_remove_root(void **slot) {
    ensure_backend();
    gc_backend->remove_root(slot);
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
