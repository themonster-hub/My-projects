// mem debug
#ifndef MEMDEBUG_MEMORY_H
#define MEMDEBUG_MEMORY_H

#include <stddef.h>

void* memdebug_malloc(size_t size, const char* file, int line);
void* memdebug_calloc(size_t num, size_t size, const char* file, int line);
void* memdebug_realloc(void* ptr, size_t size, const char* file, int line);
void memdebug_free(void* ptr, const char* file, int line);

void memdebug_init(void);
void memdebug_finalize(void);
void memdebug_dump_leaks(void);
size_t memdebug_get_allocated(void);

#define MALLOC(size) memdebug_malloc(size, __FILE__, __LINE__)
#define CALLOC(num, size) memdebug_calloc(num, size, __FILE__, __LINE__)
#define REALLOC(ptr, size) memdebug_realloc(ptr, size, __FILE__, __LINE__)
#define FREE(ptr) memdebug_free(ptr, __FILE__, __LINE__)

#endif
