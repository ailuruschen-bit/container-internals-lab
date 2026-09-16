# Labs — Chapter 04: cgroups v2

All labs need a cgroup v2 host (`stat -fc %T /sys/fs/cgroup` → `cgroup2fs`) and a
root shell. Each lab creates its own cgroup directly under `/sys/fs/cgroup`
(like the `cgroupfs` driver) and removes it at the end. Every lab defines the
same helper, which runs a command inside a cgroup using the create → configure →
execute pattern:

```bash
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }
```

| Lab | Topic | Doc section |
|---|---|---|
| [lab-01-cgroupfs-basics](lab-01-cgroupfs-basics/) | create, join, enable controllers, no internal processes, freeze, kill | [§2](../../docs/04-cgroups/02-hierarchy-and-cgroupfs.md) |
| [lab-02-memory-controller](lab-02-memory-controller/) | `memory.max`, `memory.high`, page cache, tmpfs, group OOM | [§3](../../docs/04-cgroups/03-memory-controller.md) |
| [lab-03-cpu-controller](lab-03-cpu-controller/) | quotas, throttling, weights, cpusets | [§4](../../docs/04-cgroups/04-cpu-controller.md) |
| [lab-04-pids-controller](lab-04-pids-controller/) | `pids.max`, threads, `RLIMIT_NPROC`, contained fork bomb | [§5](../../docs/04-cgroups/05-pids-and-io-controllers.md) |
| [lab-05-io-controller](lab-05-io-controller/) | `io.max` for direct and buffered writes | [§5](../../docs/04-cgroups/05-pids-and-io-controllers.md) |
| [lab-06-cgroup-namespace-and-jvm](lab-06-cgroup-namespace-and-jvm/) | cgroup namespace, JVM ergonomics, heap OOM vs kernel OOM kill | [§6](../../docs/04-cgroups/06-cgroup-namespace.md), [§7](../../docs/04-cgroups/07-containers-and-jvm.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential python3 util-linux procps openjdk-21-jdk-headless
```

If a lab is interrupted, remove leftovers with:

```bash
for d in /sys/fs/cgroup/lab*; do echo 1 | sudo tee $d/cgroup.kill >/dev/null; sleep 0.5; sudo rmdir $d/* $d 2>/dev/null; done
```

Expected observations were written for Linux 6.x with systemd. Numbers such as
throttled periods and allocation sizes at the moment of an OOM kill vary.
