#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char *p = (char*)malloc(128);
    strcpy(p, "hello from third_party");
    // Intentionally leak p

    char *q = (char*)malloc(32);
    free(q);

    printf("Done third_party.\n");
    return 0;
}