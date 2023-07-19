#!/bin/bash
echo 4 > /proc/sys/vm/zone_reclaim_mode
echo 1 > /sys/kernel/mm/numa/demotion_enabled
sysctl -w kernel.numa_balancing=2
mkdir /sys/fs/cgroup/yyw
echo 1024 > /sys/fs/cgroup/yyw/memory.node_limit1
echo 1024 > /sys/fs/cgroup/yyw/memory.node_limit2
echo 1024 > /sys/fs/cgroup/yyw/memory.node_limit3
echo 1024 > /sys/fs/cgroup/yyw/memory.node_limit4
