// mem debug
#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

typedef struct mem_node {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    struct mem_node *next;
} mem_node;

static mem_node *head = NULL;
static size_t total_allocated = 0;

static mem_node* find_node(void *ptr) {
    mem_node *node = head;
    while (node) {
        if (node->ptr == ptr) return node;
        node = node->next;
    }
    return NULL;
}

static void remove_node(mem_node *node) {
    if (head == node) {
        head = node->next;
        return;
    }
    mem_node *prev = head;
    while (prev->next != node) prev = prev->next;
    prev->next = node->next;
}

static void insert_node(mem_node *node) {
    node->next = head;
    head = node;
}

void memdebug_init(void) {
    head = NULL;
    total_allocated = 0;
}

void* memdebug_malloc(size_t size, const char* file, int line) {
    void *ptr = malloc(size);
    if (!ptr) return NULL;
    
    mem_node *node = malloc(sizeof(mem_node));
    if (!node) {
        free(ptr);
        return NULL;
    }
    
    node->ptr = ptr;
    node->size = size;
    node->file = file;
    node->line = line;
    insert_node(node);
    total_allocated += size;
    return ptr;
}

void* memdebug_calloc(size_t num, size_t size, const char* file, int line) {
    void *ptr = calloc(num, size);
    if (!ptr) return NULL;
    
    size_t total = num * size;
    mem_node *node = malloc(sizeof(mem_node));
    if (!node) {
        free(ptr);
        return NULL;
    }
    
    node->ptr = ptr;
    node->size = total;
    node->file = file;
    node->line = line;
    insert_node(node);
    total_allocated += total;
    return ptr;
}

void* memdebug_realloc(void* ptr, size_t size, const char* file, int line) {
    if (!ptr) return memdebug_malloc(size, file, line);
    
    mem_node *node = find_node(ptr);
    if (!node) return realloc(ptr, size);
    
    remove_node(node);
    size_t old_size = node->size;
    void *new_ptr = realloc(ptr, size);
    
    if (!new_ptr) {
        insert_node(node);
        return NULL;
    }
    
    node->ptr = new_ptr;
    node->size = size;
    node->file = file;
    node->line = line;
    insert_node(node);
    total_allocated = total_allocated - old_size + size;
    return new_ptr;
}

void memdebug_free(void* ptr, const char* file, int line) {
    if (!ptr) return;
    
    mem_node *node = find_node(ptr);
    if (!node) {
        free(ptr);
        return;
    }
    
    remove_node(node);
    total_allocated -= node->size;
    free(node->ptr);
    free(node);
}

void memdebug_dump_leaks(void) {
    mem_node *node = head;
    while (node) {
        printf("Leak: %p (%zu bytes) at %s:%d\n", 
               node->ptr, node->size, node->file, node->line);
        node = node->next;
    }
}

size_t memdebug_get_allocated(void) {
    return total_allocated;
}

void memdebug_finalize(void) {
    memdebug_dump_leaks();
    mem_node *node = head;
    while (node) {
        mem_node *next = node->next;
        free(node);
        node = next;
    }
    head = NULL;
    total_allocated = 0;
}
