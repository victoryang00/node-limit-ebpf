#include "bpfattacher.h"

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

int BPFAttacher::cmp_symbols(const void *l, const void *r) {
    const GElf_Sym *lsym = (const GElf_Sym *)l;
    const GElf_Sym *rsym = (const GElf_Sym *)r;

    if (lsym->st_value < rsym->st_value)
        return -1;
    else if (lsym->st_value > rsym->st_value)
        return 1;
    else
        return 0;
}

int BPFAttacher::load_maps(struct bpf_map_data *maps, int nr_maps) {
    int i;

    for (i = 0; i < nr_maps; i++) {
        if (maps[i].def.type == BPF_MAP_TYPE_ARRAY_OF_MAPS || maps[i].def.type == BPF_MAP_TYPE_HASH_OF_MAPS) {
            int inner_map_fd = map_fd[maps[i].def.inner_map_idx];
            struct bpf_map_create_opts opt = {
                .sz = sizeof(opt),
                .inner_map_fd = static_cast<__u32>(inner_map_fd),
                .map_flags = maps[i].def.map_flags,
                .numa_node = 0,
            };

            map_fd[i] = bpf_map_create(((enum bpf_map_type)maps[i].def.type), "my_map", maps[i].def.key_size,
                                       maps[i].def.value_size, maps[i].def.max_entries, &opt);
        } else {
            struct bpf_map_create_opts opt = {
                .sz = sizeof(opt),
                .map_flags = maps[i].def.map_flags,
                .numa_node = 0,
            };
            map_fd[i] = bpf_map_create(((enum bpf_map_type)maps[i].def.type), "my_map", maps[i].def.key_size,
                                       maps[i].def.value_size, maps[i].def.max_entries, &opt);
        }
        if (map_fd[i] < 0) {
            LOG(ERROR) << fmt::format("failed to create a map: {} {}\n", errno, strerror(errno));
            return 1;
        }
        maps[i].fd = map_fd[i];

        if (maps[i].def.type == BPF_MAP_TYPE_PROG_ARRAY)
            prog_array_fd = map_fd[i];
    }
    return 0;
}

int BPFAttacher::parse_relo_and_apply(Elf_Data *data, Elf_Data *symbols, GElf_Shdr *shdr, struct bpf_insn *insn,
                                struct bpf_map_data *maps, int nr_maps) {
    int i, nrels;

    nrels = shdr->sh_size / shdr->sh_entsize;

    for (i = 0; i < nrels; i++) {
        GElf_Sym sym;
        GElf_Rel rel;
        unsigned int insn_idx;
        bool match = false;
        int map_idx;

        gelf_getrel(data, i, &rel);

        insn_idx = rel.r_offset / sizeof(struct bpf_insn);

        gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym);

        if (insn[insn_idx].code != (BPF_LD | BPF_IMM | BPF_DW)) {
            LOG(ERROR) << fmt::format("invalid relo for insn[{}].code 0x%x\n", insn_idx, insn[insn_idx].code);
            return 1;
        }
        insn[insn_idx].src_reg = BPF_PSEUDO_MAP_FD;

        /* Match FD relocation against recorded map_data[] offset */
        for (map_idx = 0; map_idx < nr_maps; map_idx++) {
            if (maps[map_idx].elf_offset == sym.st_value) {
                match = true;
                break;
            }
        }
        if (match) {
            insn[insn_idx].imm = maps[map_idx].fd;
        } else {
            LOG(ERROR) << fmt::format("invalid relo for insn[{}] no map_data match\n", insn_idx);
            return 1;
        }
    }

    return 0;
}

 int BPFAttacher::get_sec(Elf *elf, int i, GElf_Ehdr *ehdr, char **shname, GElf_Shdr *shdr, Elf_Data **data) {
    Elf_Scn *scn;

    scn = elf_getscn(elf, i);
    if (!scn)
        return 1;

    if (gelf_getshdr(scn, shdr) != shdr)
        return 2;

    *shname = elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name);
    if (!*shname || !shdr->sh_size)
        return 3;

    *data = elf_getdata(scn, 0);
    if (!*data || elf_getdata(scn, *data) != nullptr)
        return 4;

    return 0;
}

int BPFAttacher::load_elf_maps_section(struct bpf_map_data *maps, int maps_shndx, Elf *elf, Elf_Data *symbols,
                                 int strtabidx) {
    int map_sz_elf, map_sz_copy;
    bool validate_zero = false;
    Elf_Data *data_maps;
    int i, nr_maps;
    GElf_Sym *sym;
    Elf_Scn *scn;
    int copy_sz;

    if (maps_shndx < 0)
        return -EINVAL;
    if (!symbols)
        return -EINVAL;

    /* Get data for maps section via elf index */
    scn = elf_getscn(elf, maps_shndx);
    if (scn)
        data_maps = elf_getdata(scn, NULL);
    if (!scn || !data_maps) {
        printf("Failed to get Elf_Data from maps section {}\n", maps_shndx);
        return -EINVAL;
    }

    /* For each map get corrosponding symbol table entry */
    sym = static_cast<GElf_Sym *>(calloc(MAX_MAPS + 1, sizeof(GElf_Sym)));
    for (i = 0, nr_maps = 0; i < symbols->d_size / sizeof(GElf_Sym); i++) {
        if (!gelf_getsym(symbols, i, &sym[nr_maps]))
            continue;
        if (sym[nr_maps].st_shndx != maps_shndx)
            continue;
        /* Only increment iif maps section */
        nr_maps++;
    }

    /* Align to map_fd[] order, via sort on offset in sym.st_value */
    qsort(sym, nr_maps, sizeof(GElf_Sym), cmp_symbols);

    map_sz_elf = data_maps->d_size / nr_maps;
    map_sz_copy = sizeof(struct bpf_map_def);
    if (map_sz_elf < map_sz_copy) {
        /*
         * Backward compat, loading older ELF file with
         * smaller struct, keeping remaining bytes zero.
         */
        map_sz_copy = map_sz_elf;
    } else if (map_sz_elf > map_sz_copy) {
        /*
         * Forward compat, loading newer ELF file with larger
         * struct with unknown features. Assume zero means
         * feature not used.  Thus, validate rest of struct
         * data is zero.
         */
        validate_zero = true;
    }

    /* Memcpy relevant part of ELF maps data to loader maps */
    for (i = 0; i < nr_maps; i++) {
        unsigned char *addr, *end;
        struct bpf_map_def *def;
        const char *map_name;
        size_t offset;

        map_name = elf_strptr(elf, strtabidx, sym[i].st_name);
        maps[i].name = strdup(map_name);
        if (!maps[i].name) {
            printf("strdup({}): {}({})\n", map_name, strerror(errno), errno);
            free(sym);
            return -errno;
        }

        /* Symbol value is offset into ELF maps section data area */
        offset = sym[i].st_value;
        def = (struct bpf_map_def *)(((long)data_maps->d_buf) + offset);
        maps[i].elf_offset = offset;
        memset(&maps[i].def, 0, sizeof(struct bpf_map_def));
        memcpy(&maps[i].def, def, map_sz_copy);

        /* Verify no newer features were requested */
        if (validate_zero) {
            addr = (unsigned char *)def + map_sz_copy;
            end = (unsigned char *)def + map_sz_elf;
            for (; addr < end; addr++) {
                if (*addr != 0) {
                    free(sym);
                    return -EFBIG;
                }
            }
        }
    }

    free(sym);
    return nr_maps;
}

perf_event_attr BPFAttacher::load_and_attach(const char *event, struct bpf_insn *prog, int size, int pid, int cpu) {
    bool is_socket = strncmp(event, "socket", 6) == 0;
    bool is_kprobe = strncmp(event, "kprobe/", 7) == 0;
    bool is_kretprobe = strncmp(event, "kretprobe/", 10) == 0;
    bool is_tracepoint = strncmp(event, "tracepoint/", 11) == 0;
    bool is_xdp = strncmp(event, "xdp", 3) == 0;
    bool is_perf_event = strncmp(event, "perf_event", 10) == 0;
    bool is_cgroup_skb = strncmp(event, "cgroup/skb", 10) == 0;
    bool is_cgroup_sk = strncmp(event, "cgroup/sock", 11) == 0;
    size_t insns_cnt = size / sizeof(struct bpf_insn);
    enum bpf_prog_type prog_type;
    char buf[256];
    int fd, efd, err, id;
    struct perf_event_attr attr = {};

    attr.type = PERF_TYPE_TRACEPOINT;
    attr.sample_type = PERF_SAMPLE_RAW;
    attr.sample_period = 1;
    attr.wakeup_events = 1;

    if (is_socket) {
        prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    } else if (is_kprobe || is_kretprobe) {
        prog_type = BPF_PROG_TYPE_KPROBE;
    } else if (is_tracepoint) {
        prog_type = BPF_PROG_TYPE_TRACEPOINT;
    } else if (is_xdp) {
        prog_type = BPF_PROG_TYPE_XDP;
    } else if (is_perf_event) {
        prog_type = BPF_PROG_TYPE_PERF_EVENT;
    } else if (is_cgroup_skb) {
        prog_type = BPF_PROG_TYPE_CGROUP_SKB;
    } else if (is_cgroup_sk) {
        prog_type = BPF_PROG_TYPE_CGROUP_SOCK;
    } else {
        LOG(ERROR) << fmt::format("Unknown event '{}'\n", event);
        throw;
    }

    fd = bpf_load_program(prog_type, prog, insns_cnt, license, kern_version, bpf_log_buf, BPF_LOG_BUF_SIZE);
    if (fd < 0) {
        LOG(ERROR) << fmt::format("bpf_load_program() err={}\n{}", errno, bpf_log_buf);
        throw;
    }

    prog_fd = fd;

    if (is_kprobe || is_kretprobe) {
        if (is_kprobe)
            event += 7;
        else
            event += 10;

        if (*event == 0) {
            LOG(ERROR) << fmt::format("event name cannot be empty\n");
            throw;
        }
        std::string event_str(event);
        if (is_kprobe) {
            LOG(INFO) << fmt::format("echo '{}:{} {}' >> /sys/kernel/debug/tracing/kprobe_events\n", 'p',
                                     event_str + "_kprobe", event);
            snprintf(buf, sizeof(buf), "echo '%c:%s %s' >> /sys/kernel/debug/tracing/kprobe_events", 'p',
                     (event_str + "_kprobe").c_str(), event);
        } else {
            LOG(INFO) << fmt::format("echo '{}:{} {}' >> /sys/kernel/debug/tracing/kprobe_events\n", 'r',
                                     event_str + "_kretprobe", event);
            snprintf(buf, sizeof(buf), "echo '%c:%s %s' >> /sys/kernel/debug/tracing/kprobe_events", 'r',
                     (event_str + "_kretprobe").c_str(), event);
        }
        err = system(buf);

        if (err < 0) {
            LOG(ERROR) << fmt::format("failed to create kprobe '{}' error '{}'\n", event, strerror(errno));
        }

        strcpy(buf, DEBUGFS);
        strcat(buf, "events/kprobes/");
        if (is_kprobe)
            strcat(buf, (event_str + "_kprobe").c_str());
        else
            strcat(buf, (event_str + "_kretprobe").c_str());
        strcat(buf, "/id");
    } else if (is_tracepoint) {
        event += 11;

        if (*event == 0) {
            LOG(ERROR) << fmt::format("event name cannot be empty\n");
            throw;
        }
        strcpy(buf, DEBUGFS);
        strcat(buf, "events/");
        strcat(buf, event);
        strcat(buf, "/id");
    }

    efd = open(buf, O_RDONLY, 0);
    if (efd < 0) {
        LOG(ERROR) << fmt::format("failed to open event {} {}\n", event,buf);
        throw;
    }

    err = read(efd, buf, sizeof(buf));
    if (err < 0 || err >= sizeof(buf)) {
        LOG(ERROR) << fmt::format("read from '{}' failed '{}'\n", event, strerror(errno));
        throw;
    }

    close(efd);

    buf[err] = 0;
    id = atoi(buf);
    attr.config = id;

    efd = perf_event_open(&attr, pid, cpu, -1, 0);
    if (efd < 0) {
        LOG(ERROR) << fmt::format("event {} fd {} err {}\n", id, efd, strerror(errno));
        throw;
    }
    event_fd = efd;
    ioctl(efd, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(efd, PERF_EVENT_IOC_SET_BPF, fd);
    return attr;
}

BPFAttacher::BPFAttacher(int group_fd, int cpu, const char *bpf_filename, pid_t pid, unsigned long flags)
    : group_fd(group_fd), cpu(cpu), pid(pid), flags(flags) {

    int fd, i, ret, maps_shndx = -1, strtabidx = -1;
    struct perf_event_attr attr {};
    Elf *elf;
    GElf_Ehdr ehdr;
    GElf_Shdr shdr, shdr_prog;
    Elf_Data *data, *data_prog, *data_maps = nullptr, *symbols = nullptr;
    char *shname, *shname_prog;
    int nr_maps = 0;

    /* reset global variables */
    kern_version = 0;
    memset(license, 0, sizeof(license));
    memset(processed_sec, 0, sizeof(processed_sec));

    if (elf_version(EV_CURRENT) == EV_NONE)
        throw;

    fd = open(bpf_filename, O_RDONLY, 0);
    if (fd < 0)
        throw;

    elf = elf_begin(fd, ELF_C_READ, nullptr);

    if (!elf)
        throw;

    if (gelf_getehdr(elf, &ehdr) != &ehdr)
        throw;

    /* clear all kprobes */
    i = system("echo \"\" > /sys/kernel/debug/tracing/kprobe_events");

    /* scan over all elf sections to get license and map info */
    for (i = 1; i < ehdr.e_shnum; i++) {

        if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
            continue;

        if (strcmp(shname, "license") == 0) {
            processed_sec[i] = true;
            memcpy(license, data->d_buf, data->d_size);
        } else if (strcmp(shname, "version") == 0) {
            processed_sec[i] = true;
            if (data->d_size != sizeof(int)) {
                LOG(ERROR) << fmt::format("invalid size of version section %zd\n", data->d_size);
                throw;
            }
            memcpy(&kern_version, data->d_buf, sizeof(int));
        } else if (strcmp(shname, "maps") == 0) {
            int j;

            maps_shndx = i;
            data_maps = data;
            for (j = 0; j < MAX_MAPS; j++)
                map_data[j].fd = -1;
        } else if (shdr.sh_type == SHT_SYMTAB) {
            strtabidx = shdr.sh_link;
            symbols = data;
        }
    }

    ret = 1;

    if (!symbols) {
        LOG(ERROR) << fmt::format("missing SHT_SYMTAB section\n");
        throw;
    }

    if (data_maps) {
        nr_maps = load_elf_maps_section(map_data, maps_shndx, elf, symbols, strtabidx);
        if (nr_maps < 0) {
            LOG(ERROR) << fmt::format("Error: Failed loading ELF maps (errno:{}):{}\n", nr_maps, strerror(-nr_maps));
            ret = 1;
            throw;
        }
        if (load_maps(map_data, nr_maps))
            throw;
        map_data_count = nr_maps;

        processed_sec[maps_shndx] = true;
    }

    /* load programs that need map fixup (relocations) */
    for (i = 1; i < ehdr.e_shnum; i++) {
        if (processed_sec[i])
            continue;

        if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
            continue;
        if (shdr.sh_type == SHT_REL) {
            struct bpf_insn *insns;

            if (get_sec(elf, shdr.sh_info, &ehdr, &shname_prog, &shdr_prog, &data_prog))
                continue;

            if (shdr_prog.sh_type != SHT_PROGBITS || !(shdr_prog.sh_flags & SHF_EXECINSTR))
                continue;

            insns = (struct bpf_insn *)data_prog->d_buf;

            processed_sec[shdr.sh_info] = true;
            processed_sec[i] = true;

            if (parse_relo_and_apply(data, symbols, &shdr, insns, map_data, nr_maps))
                continue;

            if (memcmp(shname_prog, "kprobe/", 7) == 0 || memcmp(shname_prog, "kretprobe/", 10) == 0 ||
                memcmp(shname_prog, "tracepoint/", 11) == 0 || memcmp(shname_prog, "xdp", 3) == 0 ||
                memcmp(shname_prog, "perf_event", 10) == 0 || memcmp(shname_prog, "socket", 6) == 0 ||
                memcmp(shname_prog, "cgroup/", 7) == 0)
                attr = load_and_attach(shname_prog, insns, data_prog->d_size, pid, cpu);
            this->fd = perf_event_open(&attr, this->pid, this->cpu, this->group_fd, this->flags);
            if (this->fd == -1) {
                LOG(ERROR) << "perf_event_open";
                throw;
            }
            ioctl(this->fd, PERF_EVENT_IOC_RESET, 0);
            bpf_fd = event_fd;
        }
    }

    /* load programs that don't use maps */
    for (i = 1; i < ehdr.e_shnum; i++) {

        if (processed_sec[i])
            continue;

        if (get_sec(elf, i, &ehdr, &shname, &shdr, &data))
            continue;

        if (memcmp(shname, "kprobe/", 7) == 0 || memcmp(shname, "kretprobe/", 10) == 0 ||
            memcmp(shname, "tracepoint/", 11) == 0 || memcmp(shname, "xdp", 3) == 0 ||
            memcmp(shname, "perf_event", 10) == 0 || memcmp(shname, "socket", 6) == 0 ||
            memcmp(shname, "cgroup/", 7) == 0)
            attr = load_and_attach(shname, (struct bpf_insn *)data->d_buf, data->d_size, pid, cpu);
        this->fd = perf_event_open(&attr, this->pid, this->cpu, this->group_fd, this->flags);
        if (this->fd == -1) {
            throw std::runtime_error("perf_event_open");
        }
    }
    bpf_fd = event_fd;
    ioctl(this->fd, PERF_EVENT_IOC_RESET, 0);
}

BPFAttacher::~BPFAttacher() {
    if (this->fd != -1) {
        close(this->fd);
        this->fd = -1;
    }
}

int BPFAttacher::start() {
    if (ioctl(this->fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
        printf("ioctl");
        return -1;
    }
    return 0;
}

int BPFAttacher::stop() {
    if (ioctl(this->fd, PERF_EVENT_IOC_DISABLE, 0) < 0) {
        printf("ioctl");
        return -1;
    }
    return 0;
}
