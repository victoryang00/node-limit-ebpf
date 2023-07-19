//
// Created by root on 4/10/23.
//

#ifndef BEDE_RSS_H
#define BEDE_RSS_H

#include "logging.h"
#include <iostream>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

class Rss {
public:
    int rss;
    int fd;

    Rss(const char *socket_path);

    uint64_t update_local_rss(int pid);

    ~Rss();
};

#endif // BEDE_RSS_H
