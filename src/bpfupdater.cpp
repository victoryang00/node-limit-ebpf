//
// Created by root on 4/9/23.
//

#include "bpfupdater.h"

template <typename K, typename V> BPFUpdater<K, V>::BPFUpdater(int map_fd) : map_fd(map_fd) {}

template <typename K, typename V> void BPFUpdater<K, V>::update(K key, V value) {
    int ret = bpf_map_update_elem(map_fd, &key, &value, BPF_ANY);
    if (ret != 0) {
        LOG(ERROR) << fmt::format("Error updating map: {}\n", strerror(errno));
        throw std::runtime_error("Error updating the bpf map");
    }
}
