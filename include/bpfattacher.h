//
// Created by victoryang00 on 4/8/23.
//

#ifndef BEDE_PERF_H
#define BEDE_PERF_H

#include "logging.h"
#include <bpf/bpf.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <gelf.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>

#define MAX_MAPS 32
#define DEBUGFS "/sys/kernel/debug/tracing/"

struct bpf_map_def {
    unsigned int type;
    unsigned int key_size;
    unsigned int value_size;
    unsigned int max_entries;
    unsigned int map_flags;
    unsigned int inner_map_idx;
};
struct bpf_map_data {
    int fd;
    char *name;
    size_t elf_offset;
    struct bpf_map_def def;
};

class BPFAttacher {
public:
    int fd;
    int group_fd;
    int bpf_fd;
    pid_t pid;
    int cpu;
    char license[128];
    int kern_version;
    bool processed_sec[128];
    char bpf_log_buf[BPF_LOG_BUF_SIZE];
    int map_fd[MAX_MAPS];
    int prog_fd;
    int event_fd;
    int prog_array_fd = -1;
    struct bpf_map_data map_data[MAX_MAPS];
    int map_data_count = 0;
    unsigned long flags;

    BPFAttacher(int group_fd, int cpu, const char *bpf_filename, pid_t pid, unsigned long flags);

    static int parse_relo_and_apply(Elf_Data *data, Elf_Data *symbols, GElf_Shdr *shdr, struct bpf_insn *insn,
                                    struct bpf_map_data *maps, int nr_maps);
    static int get_sec(Elf *elf, int i, GElf_Ehdr *ehdr, char **shname, GElf_Shdr *shdr, Elf_Data **data);
    static int load_elf_maps_section(struct bpf_map_data *maps, int maps_shndx, Elf *elf, Elf_Data *symbols,
                                     int strtabidx);
    static int cmp_symbols(const void *l, const void *r);
    int load_maps(struct bpf_map_data *maps, int nr_maps);
    perf_event_attr load_and_attach(const char *event, struct bpf_insn *prog, int size, int pid, int cpu);

    ~BPFAttacher();

    int start();

    int stop();
};

#endif // BEDE_PERF_H