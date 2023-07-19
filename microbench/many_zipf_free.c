#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int rand_malloc_size(int n) { return (int)(rand() / n); }

int main() {
    srand(time(NULL));
    unsigned long long n = 5000 * 1024ULL * 1024ULL; // Maximum allocation size
    void *ptr[8] = {0};

    // Example usage
    for (int i = 0; i < 8; i++) {
        ptr[i] = malloc(n);
        for (int j = 0; j < n; j++) {
            ((char *)ptr[i])[j] = 'w';
        }
    }
#pragma omp parallel for
    for (int i = 0; i < 8; i++) {
        if (ptr[i] != NULL) {
            free(ptr[i]);
        }
    }
#pragma omp parallel for
    for (int i = 0; i < 8; i++) {
        int slep = rand() % 100;
        printf("%d\n", slep);
        sleep(slep);
        if (ptr[i] != NULL) {
            free(ptr[i]);
        }
    }
    return 0;
}