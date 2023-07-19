#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void my_constructor(void) __attribute__((constructor));

void my_constructor(void) {
    printf("Constructor called\n");
}
int main() {
    int *p;
#pragma omp parallel for
    for (int i = 0; i < 60 * 1024 * 1024; i++) {
        int inc = 1024 * sizeof(char);
        p = (int *)calloc(1, inc);
        if (!p)
            printf("error");
    }
    sleep(100);
}