// contact to the server to change the state of the local rss bound.

#include "bpfattacher.h"
#include "bpfupdater.cpp"
#include "rss.h"
#include <cxxopts.hpp>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[], char *env[]) {
    cxxopts::Options options(
        "Bede", "For Bede: Get the Most Bang for your DRAM Buck with Historical Workload-Aware CXL-Memory Allocation");
    options.add_options()("t,target", "The script file to execute",
                          cxxopts::value<std::string>()->default_value("./microbench/many_calloc"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"))(
        "p,pid", "The value for passed pid", cxxopts::value<int>()->default_value("0"))(
        "s,socket", "The value for socket path", cxxopts::value<std::string>()->default_value("/tmp/slugalloc.sock"))(
        "r,localrss", "The value for targeted local rss in MB", cxxopts::value<int>()->default_value("1000"))(
        "w,watermark_path", "The value for watermark path",
        cxxopts::value<std::string>()->default_value("./watermark.o"));

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto pid = result["pid"].as<int>();
    auto target = result["target"].as<std::string>();
    uint64_t res = 0;
    auto watermark_path = result["watermark_path"].as<std::string>().c_str();
    char cmd_buf[1024] = {0};
    strncpy(cmd_buf, target.c_str(), sizeof(cmd_buf));

    /* This strtok_r() call puts '\0' after the first token in the buffer,
     * It saves the state to the strtok_state and subsequent calls resume from that point. */
    char *strtok_state = nullptr;
    char *filename = strtok_r(cmd_buf, " ", &strtok_state);

    /* Allocate an array of pointers.
     * We will make them point to certain locations inside the cmd_buf. */
    char *args[32] = {nullptr};
    /* loop the strtok_r() call while there are tokens and free space in the array */

    args[0] = filename;

    size_t current_arg_idx;
    for (current_arg_idx = 1; current_arg_idx < 32; ++current_arg_idx) {
        /* Note that the first argument to strtok_r() is nullptr.
         * That means resume from a point saved in the strtok_state. */
        char *current_arg = strtok_r(nullptr, " ", &strtok_state);
        if (current_arg == nullptr) {
            break;
        }

        args[current_arg_idx] = current_arg;
        LOG(ERROR) << fmt::format("args[{}] = {}\n", current_arg_idx, args[current_arg_idx]);
    }

    if (pid == 0) {
        pid = fork();
        if (pid == 0) {
            execve(filename, args, env);
            exit(1);
        }
    }

    auto bpf_attatcher = new BPFAttacher(-1, -1, watermark_path, pid, 0);
    bpf_attatcher->start();
    auto bpf_updater = new BPFUpdater<uint64_t, uint64_t>(bpf_attatcher->map_fd[0]);

    auto rss = new Rss(result["socket"].as<std::string>().c_str());
    rss->rss = result["localrss"].as<int>() + 1;
    bpf_updater->update(pid, ((uint64_t)rss->rss) << 32);
    LOG(WARNING) << "Pid is " << pid << "\n";
    nodemask_t nodemask1 = {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    nodemask_t nodemask2 = {{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    auto bpf_updater_nodemask = new BPFUpdater<uint32_t, nodemask_t>(bpf_attatcher->map_fd[1]);
    bpf_updater_nodemask->update(1, nodemask1);
    bpf_updater_nodemask->update(2, nodemask2);
    while (true) {
        struct timespec sleep_duration;
        struct timespec remaining_time;

        sleep_duration.tv_sec = 0;
        ;
        sleep_duration.tv_nsec = 5000; // 50 miliseconds (200 per minute)

        auto ret = nanosleep(&sleep_duration, &remaining_time);

        if (nanosleep(&sleep_duration, &remaining_time) == -1) {
            LOG(ERROR) << "nanosleep() failed\n";
            return 1;
        }
        res = rss->update_local_rss(pid);
        // std::cerr << res << "\n";
        if (res == 0) {
            return 0;
        } else {
            bpf_updater->update(pid, res);
        }
    }
    return 0;
}
