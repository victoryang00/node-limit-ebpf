#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define PAGE_SIZE 4096

int main() {
    // Allocate memory using mmap
    void *region = mmap(NULL, PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (region == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // Trigger a page fault by trying to write to the protected page
    char *ptr = (char *)region;
    ptr[0] = 'A'; // This will trigger a SIGSEGV
    sleep(100);
    return 0;
}