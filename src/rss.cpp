//
// Created by root on 4/10/23.
//

#include "rss.h"

Rss::Rss(const char *socket_path) {
    struct sockaddr_un addr;
    char buf[100];
    int var = 0; // variable to be changed by client

    // Create a socket
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        LOG(ERROR) << "socket error";
        throw std::runtime_error("socket error");
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG(ERROR) << "Failed to set socket to non-blocking mode\n";
        throw std::runtime_error("fcntl error");
    }

    // Bind the socket to an address
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    unlink(socket_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG(ERROR) << "bind error";
        throw std::runtime_error("bind error");
    }
    // Listen for incoming connections
    if (listen(fd, 5) == -1) {
        LOG(ERROR) << "listen error";
        throw std::runtime_error("listen error");
    }
}

uint64_t Rss::update_local_rss(int pid) {
    int rc;
    char buf[100];
    int var, client_fd, status;
    long long local_rss = 0;
    std::ifstream numa_maps_file;
    std::string line;
    std::string path = "/proc/" + std::to_string(pid) + "/numa_maps";
    numa_maps_file.open(path);

    if (!numa_maps_file.is_open()) {
        LOG(ERROR) << "Cannot find " << path << ". Ensure the process exists.\n";
        return 0;
    }

    while (std::getline(numa_maps_file, line)) {
        std::istringstream line_stream(line);
        std::string field;
        while (line_stream >> field) {
            if (field.find("N0") == 0) {
                std::size_t equal_sign_pos = field.find("=");
                int local_pages = std::stoi(field.substr(equal_sign_pos + 1));
                local_rss += local_pages;
            }
        }
    }

    numa_maps_file.close();

    local_rss = (double)local_rss / 256;

    // LOG(INFO) << "local_rss: " << local_rss << "\n";
    //  Accept a client connection
    if (local_rss == 0)
        return 0;
    if ((client_fd = accept(fd, NULL, NULL)) == -1) {
        LOG(DEBUG) << "accepting\n";
        return (((uint64_t)this->rss) << 32) + local_rss;
    }
    if ((rc = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
        if (strncmp(buf, "set_var:", 8) == 0) {
            sscanf(buf, "set_var:%d", &var); // use std::sscanf
            LOG(INFO) << fmt::format("Variable set to {}\n", var);
            this->rss = var;
        }
    }
    close(client_fd);

    return (((uint64_t)this->rss) << 32) + local_rss;
}
Rss::~Rss() { close(fd); }
