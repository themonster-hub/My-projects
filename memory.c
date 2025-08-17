// mem debug
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <execinfo.h>
#ifdef __linux__
#include <unistd.h>
#endif
#include "memory.h"

#ifdef MEMDEBUG_INTERPOSE
#include <dlfcn.h>
#endif

#ifndef MEMDEBUG_DEFAULT_BUCKETS
#define MEMDEBUG_DEFAULT_BUCKETS 4096
#endif

#ifndef MEMDEBUG_MAX_BACKTRACE
#define MEMDEBUG_MAX_BACKTRACE 16
#endif

typedef struct mem_node {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    int bt_depth;
    void *bt[MEMDEBUG_MAX_BACKTRACE];
    struct mem_node *next;
} mem_node;

static mem_node **buckets = NULL;
static size_t bucket_count = MEMDEBUG_DEFAULT_BUCKETS;
static pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static size_t allocation_count = 0;
static size_t free_count = 0;
static size_t outstanding_count = 0;
static int backtrace_enabled = 0;
static int backtrace_depth = 12;
static int initialized = 0;
static int enabled = 1;
static int abort_on_leak = 0;
static FILE *log_fp = NULL;

#ifdef MEMDEBUG_INTERPOSE
static void *(*real_malloc)(size_t) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;
static void (*real_free)(void *) = NULL;
static __thread int in_hook = 0;

static void resolve_real(void) {
    if (real_malloc) return;
    in_hook = 1;
    real_malloc = (void *(*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    real_calloc = (void *(*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
    real_realloc = (void *(*)(void *, size_t)) dlsym(RTLD_NEXT, "realloc");
    real_free = (void (*)(void *)) dlsym(RTLD_NEXT, "free");
    in_hook = 0;
}
#else
static int in_hook = 0;
#endif

static inline size_t hash_ptr(void *p) {
    uintptr_t v = (uintptr_t)p;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return (size_t)v;
}

static inline void *internal_malloc(size_t n) {
#ifdef MEMDEBUG_INTERPOSE
    resolve_real();
    return real_malloc ? real_malloc(n) : malloc(n);
#else
    return malloc(n);
#endif
}

static inline void *internal_calloc(size_t n, size_t m) {
#ifdef MEMDEBUG_INTERPOSE
    resolve_real();
    return real_calloc ? real_calloc(n, m) : calloc(n, m);
#else
    return calloc(n, m);
#endif
}

static inline void *internal_realloc(void *p, size_t n) {
#ifdef MEMDEBUG_INTERPOSE
    resolve_real();
    return real_realloc ? real_realloc(p, n) : realloc(p, n);
#else
    return realloc(p, n);
#endif
}

static inline void internal_free(void *p) {
#ifdef MEMDEBUG_INTERPOSE
    resolve_real();
    if (real_free) {
        real_free(p);
        return;
    }
#endif
    free(p);
}

static void capture_backtrace(int *depth_out, void **buffer) {
    if (!backtrace_enabled) {
        *depth_out = 0;
        return;
    }
    int depth = backtrace(buffer, backtrace_depth);
    if (depth < 0) depth = 0;
    *depth_out = depth;
}

static void buckets_init_if_needed(void) {
    if (buckets) return;
    buckets = (mem_node **)internal_calloc(bucket_count, sizeof(mem_node *));
}

static mem_node *find_node_unsafe(void *ptr, size_t *bucket_index_out, mem_node **prev_out) {
    size_t idx = hash_ptr(ptr) % bucket_count;
    mem_node *prev = NULL;
    mem_node *node = buckets[idx];
    while (node) {
        if (node->ptr == ptr) {
            if (bucket_index_out) *bucket_index_out = idx;
            if (prev_out) *prev_out = prev;
            return node;
        }
        prev = node;
        node = node->next;
    }
    return NULL;
}

static void insert_node_unsafe(mem_node *node) {
    size_t idx = hash_ptr(node->ptr) % bucket_count;
    node->next = buckets[idx];
    buckets[idx] = node;
}

static void remove_node_unsafe(size_t idx, mem_node *prev, mem_node *node) {
    if (!prev) {
        buckets[idx] = node->next;
    } else {
        prev->next = node->next;
    }
}

static void track_alloc_unsafe(void *ptr, size_t size, const char *file, int line) {
    if (!ptr) return;
    mem_node *node = (mem_node *)internal_malloc(sizeof(mem_node));
    if (!node) return;
    node->ptr = ptr;
    node->size = size;
    node->file = file ? file : "n/a";
    node->line = line;
    capture_backtrace(&node->bt_depth, node->bt);
    insert_node_unsafe(node);
    total_allocated += size;
    if (total_allocated > peak_allocated) peak_allocated = total_allocated;
    allocation_count += 1;
    outstanding_count += 1;
}

static size_t track_free_unsafe(void *ptr) {
    size_t idx = 0;
    mem_node *prev = NULL;
    mem_node *node = find_node_unsafe(ptr, &idx, &prev);
    if (!node) return 0;
    remove_node_unsafe(idx, prev, node);
    size_t s = node->size;
    internal_free(node);
    if (s <= total_allocated) total_allocated -= s; else total_allocated = 0;
    free_count += 1;
    if (outstanding_count) outstanding_count -= 1;
    return s;
}

void memdebug_init(void) {
    if (initialized) return;
#ifdef MEMDEBUG_INTERPOSE
    resolve_real();
#endif
    const char *env = getenv("MEMDEBUG");
    if (env && *env) enabled = atoi(env) != 0;
    env = getenv("MEMDEBUG_BUCKETS");
    if (env && *env) {
        size_t v = (size_t)strtoull(env, NULL, 10);
        if (v >= 64 && v <= (1u<<24)) bucket_count = v;
    }
    env = getenv("MEMDEBUG_BACKTRACE");
    if (env && *env) backtrace_enabled = atoi(env) != 0;
    env = getenv("MEMDEBUG_BT_DEPTH");
    if (env && *env) {
        int v = atoi(env);
        if (v > 0 && v <= MEMDEBUG_MAX_BACKTRACE) backtrace_depth = v;
    }
    env = getenv("MEMDEBUG_ABORT_ON_LEAK");
    if (env && *env) abort_on_leak = atoi(env) != 0;

    const char *log_path = getenv("MEMDEBUG_LOG");
    if (log_path && *log_path) {
        log_fp = fopen(log_path, "a");
        if (!log_fp) log_fp = stderr;
    } else {
        log_fp = stderr;
    }

    pthread_mutex_lock(&mem_lock);
    buckets_init_if_needed();
    initialized = 1;
    pthread_mutex_unlock(&mem_lock);
}

void* memdebug_malloc(size_t size, const char* file, int line) {
    if (!initialized) memdebug_init();
    if (!enabled) return internal_malloc(size);
    void *ptr = internal_malloc(size);
    if (!ptr) return NULL;
    pthread_mutex_lock(&mem_lock);
    track_alloc_unsafe(ptr, size, file, line);
    pthread_mutex_unlock(&mem_lock);
    return ptr;
}

void* memdebug_calloc(size_t num, size_t size, const char* file, int line) {
    if (!initialized) memdebug_init();
    if (!enabled) return internal_calloc(num, size);
    void *ptr = internal_calloc(num, size);
    if (!ptr) return NULL;
    size_t total = num * size;
    pthread_mutex_lock(&mem_lock);
    track_alloc_unsafe(ptr, total, file, line);
    pthread_mutex_unlock(&mem_lock);
    return ptr;
}

void* memdebug_realloc(void* ptr, size_t size, const char* file, int line) {
    if (!initialized) memdebug_init();
    if (!enabled) return internal_realloc(ptr, size);

    size_t old_size = 0;
    if (ptr) {
        pthread_mutex_lock(&mem_lock);
        old_size = track_free_unsafe(ptr);
        pthread_mutex_unlock(&mem_lock);
    }

    void *new_ptr = internal_realloc(ptr, size);
    if (!new_ptr && size != 0) {
        // Reinsert old pointer tracking if realloc failed (ptr still valid)
        if (ptr && old_size > 0) {
            pthread_mutex_lock(&mem_lock);
            track_alloc_unsafe(ptr, old_size, file, line);
            pthread_mutex_unlock(&mem_lock);
        }
        return NULL;
    }

    if (size == 0) return NULL;

    pthread_mutex_lock(&mem_lock);
    track_alloc_unsafe(new_ptr, size, file, line);
    pthread_mutex_unlock(&mem_lock);
    return new_ptr;
}

void memdebug_free(void* ptr, const char* file, int line) {
    if (!ptr) return;
    if (!initialized) memdebug_init();
    if (!enabled) {
        internal_free(ptr);
        return;
    }
    pthread_mutex_lock(&mem_lock);
    size_t removed = track_free_unsafe(ptr);
    pthread_mutex_unlock(&mem_lock);
    if (removed == 0) {
        fprintf(log_fp, "Warning: free of untracked pointer %p at %s:%d\n", ptr, file ? file : "n/a", line);
        fflush(log_fp);
    }
    internal_free(ptr);
}

static void dump_one_node(mem_node *node) {
    fprintf(log_fp, "Leak: %p (%zu bytes) at %s:%d\n", node->ptr, node->size, node->file, node->line);
    if (backtrace_enabled && node->bt_depth > 0) {
#ifdef __linux__
        int fd = fileno(log_fp);
        if (fd >= 0) {
            backtrace_symbols_fd(node->bt, node->bt_depth, fd);
        }
#endif
    }
}

void memdebug_dump_leaks(void) {
    if (!initialized) memdebug_init();
    size_t leaks = 0;
    pthread_mutex_lock(&mem_lock);
    for (size_t i = 0; i < bucket_count; ++i) {
        mem_node *node = buckets[i];
        while (node) {
            dump_one_node(node);
            leaks += 1;
            node = node->next;
        }
    }
    fprintf(log_fp, "Summary: outstanding=%zu, total_allocs=%zu, total_frees=%zu, current_bytes=%zu, peak_bytes=%zu, leaks=%zu\n",
            outstanding_count, allocation_count, free_count, total_allocated, peak_allocated, leaks);
    fflush(log_fp);
    pthread_mutex_unlock(&mem_lock);
}

size_t memdebug_get_allocated(void) {
    return total_allocated;
}

size_t memdebug_get_peak_allocated(void) {
    return peak_allocated;
}

size_t memdebug_get_outstanding_count(void) {
    return outstanding_count;
}

size_t memdebug_get_allocation_count(void) {
    return allocation_count;
}

void memdebug_finalize(void) {
    if (!initialized) return;
    size_t before = outstanding_count;
    memdebug_dump_leaks();
    pthread_mutex_lock(&mem_lock);
    for (size_t i = 0; i < bucket_count; ++i) {
        mem_node *node = buckets[i];
        while (node) {
            mem_node *next = node->next;
            internal_free(node);
            node = next;
        }
        buckets[i] = NULL;
    }
    if (buckets) internal_free(buckets);
    buckets = NULL;
    initialized = 0;
    pthread_mutex_unlock(&mem_lock);
    if (abort_on_leak && before > 0) {
        abort();
    }
}

void memdebug_set_enabled(int en) {
    enabled = en != 0;
}

void memdebug_enable_backtrace(int en) {
    backtrace_enabled = en != 0;
}

void memdebug_set_log_path(const char* path) {
    if (!path || !*path) return;
    FILE *fp = fopen(path, "a");
    if (!fp) return;
    pthread_mutex_lock(&mem_lock);
    if (log_fp && log_fp != stderr) fclose(log_fp);
    log_fp = fp;
    pthread_mutex_unlock(&mem_lock);
}

#ifdef MEMDEBUG_INTERPOSE
__attribute__((constructor)) static void memdebug_ctor(void) {
    memdebug_init();
}

__attribute__((destructor)) static void memdebug_dtor(void) {
    memdebug_finalize();
}

void* malloc(size_t size) {
    resolve_real();
    if (in_hook || !enabled) return real_malloc(size);
    in_hook = 1;
    void *p = real_malloc(size);
    if (p) {
        pthread_mutex_lock(&mem_lock);
        track_alloc_unsafe(p, size, "lib", 0);
        pthread_mutex_unlock(&mem_lock);
    }
    in_hook = 0;
    return p;
}

void* calloc(size_t n, size_t m) {
    resolve_real();
    if (in_hook || !enabled) return real_calloc(n, m);
    in_hook = 1;
    void *p = real_calloc(n, m);
    if (p) {
        size_t total = n * m;
        pthread_mutex_lock(&mem_lock);
        track_alloc_unsafe(p, total, "lib", 0);
        pthread_mutex_unlock(&mem_lock);
    }
    in_hook = 0;
    return p;
}

void* realloc(void *ptr, size_t n) {
    resolve_real();
    if (in_hook || !enabled) return real_realloc(ptr, n);
    in_hook = 1;
    if (ptr) {
        pthread_mutex_lock(&mem_lock);
        track_free_unsafe(ptr);
        pthread_mutex_unlock(&mem_lock);
    }
    void *np = real_realloc(ptr, n);
    if (np && n != 0) {
        pthread_mutex_lock(&mem_lock);
        track_alloc_unsafe(np, n, "lib", 0);
        pthread_mutex_unlock(&mem_lock);
    }
    in_hook = 0;
    return np;
}

void free(void *ptr) {
    resolve_real();
    if (!ptr) return;
    if (in_hook || !enabled) {
        real_free(ptr);
        return;
    }
    in_hook = 1;
    pthread_mutex_lock(&mem_lock);
    size_t removed = track_free_unsafe(ptr);
    pthread_mutex_unlock(&mem_lock);
    if (removed == 0) {
        real_free(ptr);
    } else {
        real_free(ptr);
    }
    in_hook = 0;
}
#endif
