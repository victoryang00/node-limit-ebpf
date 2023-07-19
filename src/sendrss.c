#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s socket_path rss\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *socket_path = argv[1];
    int rss = atoi(argv[2]);
    printf("Sending RSS %d\n", rss);
    struct sockaddr_un addr;
    char buf[100];
    int fd, rc;

    // Create a socket
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect error");
        exit(EXIT_FAILURE);
    }

    // Send a message to the server to set the variable
    sprintf(buf, "set_var:%d", rss);
    rc = send(fd, buf, strlen(buf) + 20, 0);
    if (rc == -1) {
        perror("send error");
        exit(EXIT_FAILURE);
    }

    // Clean up
    close(fd);

    return EXIT_SUCCESS;
}