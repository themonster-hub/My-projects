// mem debug
#ifndef MEMDEBUG_MEMORY_H
#define MEMDEBUG_MEMORY_H

#include <stddef.h>
#include <stdio.h>

void* memdebug_malloc(size_t size, const char* file, int line);
void* memdebug_calloc(size_t num, size_t size, const char* file, int line);
void* memdebug_realloc(void* ptr, size_t size, const char* file, int line);
void memdebug_free(void* ptr, const char* file, int line);

void memdebug_init(void);
void memdebug_finalize(void);
void memdebug_dump_leaks(void);
size_t memdebug_get_allocated(void);

// Additional features / controls
size_t memdebug_get_peak_allocated(void);
size_t memdebug_get_outstanding_count(void);
size_t memdebug_get_allocation_count(void);
void memdebug_set_enabled(int enabled);
void memdebug_enable_backtrace(int enabled);
void memdebug_set_log_path(const char* path);

#define MALLOC(size) memdebug_malloc(size, __FILE__, __LINE__)
#define CALLOC(num, size) memdebug_calloc(num, size, __FILE__, __LINE__)
#define REALLOC(ptr, size) memdebug_realloc(ptr, size, __FILE__, __LINE__)
#define FREE(ptr) memdebug_free(ptr, __FILE__, __LINE__)

#endif
