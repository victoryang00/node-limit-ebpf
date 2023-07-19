#/bin/bash
$@ &
pid1=$!
echo $pid1 > /sys/fs/cgroup/yyw/cgroup.procs

while true
do
 cat /sys/fs/cgroup/yyw/memory.numa_stat
 sleep 0.1
done