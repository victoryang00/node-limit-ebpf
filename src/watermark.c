#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct bpf_map_def SEC("maps") rss_map = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(u64),
    .value_size = sizeof(u64),
    .max_entries = 1,
};
struct bpf_map_def SEC("maps") nodemask_map = {
    .type = BPF_MAP_TYPE_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(nodemask_t),
    .max_entries = 3,
};

#define SECTION_SIZE_BITS 27

// this might instead by 52. who knows?
#define MAX_PHYSMEM_BITS 46
#define SECTIONS_WIDTH (MAX_PHYSMEM_BITS - SECTION_SIZE_BITS)

#define NODES_SHIFT 10
#define NODES_WIDTH NODES_SHIFT

#define NODES_MASK ((1UL << NODES_WIDTH) - 1)
#define SECTIONS_PGOFF ((sizeof(unsigned long) * 8) - SECTIONS_WIDTH)

#define NODES_PGOFF (SECTIONS_PGOFF - NODES_WIDTH)
#define NODES_PGSHIFT (NODES_PGOFF * (NODES_WIDTH != 0))

// typdef struct { DECLARE_BITMAP(bits, MAX_NUMNODES);} nodemask_t;

int page_to_nid(struct page *p) {
    int res;
    if (bpf_probe_read_kernel(&res, sizeof(res), &p->flags)) {
        char fmt[] = "fail to get res\n";
        bpf_trace_printk(fmt, sizeof(fmt));
        return 0; // Failed to read mempolicy pointer
    }

    return (res >> NODES_PGSHIFT) & NODES_MASK;
}

int policy_to_mode(struct mempolicy *p) {
    int res;
    if (bpf_probe_read_kernel(&res, sizeof(res), &p->mode)) {
        char fmt[] = "fail to get mode\n";
        bpf_trace_printk(fmt, sizeof(fmt));
        return 0; // Failed to read mempolicy pointer
    }

    return (res >> NODES_PGSHIFT) & NODES_MASK;
}

bool node_is_toptier(int srd_nid) { return srd_nid == 0; }

static inline int get_node_idx(nodemask_t *mask, int idx) {
    unsigned int n[16];
    if (bpf_probe_read_kernel(&n, sizeof(n), &mask->bits))
        return 0;
    return n[idx];
}

SEC("kretprobe/policy_node")
int track_ret_policy_node(struct pt_regs *ctx) {
    u32 orig_res = (u32)PT_REGS_RC(ctx);

    u32 res = 0;
    u64 pidtgid = bpf_get_current_pid_tgid();
    u64 tgid = (u32)(pidtgid >> 32);
    void *value = bpf_map_lookup_elem(&rss_map, &tgid);
    if (value != NULL) {
        u64 rss_local_rss = (*(u64 *)value);
        if (rss_local_rss >> 32 > (rss_local_rss - (rss_local_rss >> 32 << 32)))
            res = 0;
        else
            res = 1;
        char fmt[] = "policy_node_ret; get_timing%lld\n";
        if ((orig_res != res))
            bpf_override_return(ctx, res);
    }

    return 0;
}

SEC("kretprobe/policy_nodemask")
int track_policy_nodemask(struct pt_regs *ctx) {
    nodemask_t *nodemask = (nodemask_t *)PT_REGS_RC(ctx);
    int max_node = 16;
    u32 key = 1;

    u64 pidtgid = bpf_get_current_pid_tgid();
    u64 tgid = (u32)(pidtgid >> 32);
    void *value = bpf_map_lookup_elem(&rss_map, &tgid);
    if (value != NULL) {
        u64 rss_local_rss = (*(u64 *)value);
        if (rss_local_rss >> 32 > (rss_local_rss - (rss_local_rss >> 32 << 32)))
            key = 1;
        else
            key = 2;
        if ((get_node_idx(nodemask, 0) != key)) {
            struct node_mask *node_mask = bpf_map_lookup_elem(&nodemask_map, &key);
            if (node_mask) {
                bpf_override_return(ctx, ((u64)node_mask));
            }
        }
    }

    return 0;
}

SEC("kretprobe/bede_get_node")
int track_ret_bede_get_node(struct pt_regs *ctx) {
    int res = (int)PT_REGS_RC(ctx);

    u64 pidtgid = bpf_get_current_pid_tgid();
    u64 tgid = (u32)(pidtgid >> 32);
    char fmt[] = "bede_get_node; true %d from pid %d\n";
    bpf_trace_printk(fmt, sizeof(fmt), res, bpf_get_current_pid_tgid());
    void *value = bpf_map_lookup_elem(&rss_map, &tgid);
    if (value != NULL) {
        u64 rss_local_rss = (*(u64 *)value);
        if (rss_local_rss >> 32 > (rss_local_rss - (rss_local_rss >> 32 << 32)))
            bpf_override_return(ctx, 1);
        else
            bpf_override_return(ctx, 0);
    }

    return 0;
}

char _license[] SEC("license") = "GPL";
