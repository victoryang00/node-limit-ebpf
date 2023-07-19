# Node Limit eBPF
We reuse the watermark scanning and migration of page using the linux migrate_page API in the TPP work so that we can control the rss of the workloads.

## User input
```bash
./bede -t ./microbench/many_calloc -r 1000 -s /tmp/slugalloc.sock
```
1. -t Target: The path to the executable / -p PID: Or you can specify your pid
2. -r Local RSS: The epoch of the simulator, the parameter is in milisecond
3. -s Socket: The value for socket path

## Design in the kernel
1. We add a migrate kthread under the initialization of the cgroup.
2. Bede will migrate the lru page to the remote node while the local rss is larger than the target rss.
3. Bede will migrate the lru page to the local node while the local rss is smaller than the target rss.
