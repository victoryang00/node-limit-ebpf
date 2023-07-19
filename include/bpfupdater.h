//
// Created by root on 4/9/23.
//

#ifndef BEDE_BPFUPDATER_H
#define BEDE_BPFUPDATER_H

#include <bpf/bpf.h>
#include <iostream>
#include "logging.h"
typedef struct {
	long unsigned int bits[16];
} nodemask_t;
template<typename K, typename V>
class BPFUpdater {
    public:
    int map_fd;

    BPFUpdater(int map_fd);

    void update(K key, V value);

    ~BPFUpdater() = default;
};


#endif //BEDE_BPFUPDATER_H
