#include <stdio.h>
#include <string.h>
#include "memory.h"

static void leak_some(void) {
    char *s1 = (char*)MALLOC(16);
    char *s2 = (char*)CALLOC(4, 4);
    (void)s1; (void)s2; // intentionally leaked
}

static void mixed_ops(void) {
    char *buf = (char*)MALLOC(100);
    strcpy(buf, "hello");
    buf = (char*)REALLOC(buf, 200);
    FREE(buf);
}

int main(void) {
    memdebug_init();
    memdebug_enable_backtrace(1);

    char *a = (char*)MALLOC(32);
    char *b = (char*)CALLOC(8, 8);
    a = (char*)REALLOC(a, 64);

    leak_some();
    mixed_ops();

    FREE(b);

    printf("Currently allocated: %zu bytes (peak=%zu, outstanding=%zu, allocs=%zu)\n",
           memdebug_get_allocated(),
           memdebug_get_peak_allocated(),
           memdebug_get_outstanding_count(),
           memdebug_get_allocation_count());

    memdebug_finalize();
    return 0;
}