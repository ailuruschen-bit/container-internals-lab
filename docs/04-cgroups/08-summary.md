# 8. Chapter Summary: Bounded Consumption

## The model you should now have

A cgroup is a directory in the `cgroup2` filesystem. Processes are members;
children inherit membership across `fork()`, and `execve()` keeps it. Controllers
enabled top-down attach accounting and limits to the cgroup, and the kernel
enforces them **in the allocation paths** of each resource.

| Controller | Key files | When the limit is reached |
|---|---|---|
| memory | `memory.max`, `memory.high`, `memory.events`, `memory.stat` | `high`: throttle and reclaim. `max`: reclaim, then **cgroup OOM kill** (`SIGKILL`, exit 137) |
| cpu | `cpu.max`, `cpu.weight`, `cpu.stat` | **throttle** until the next period; weights only matter under contention |
| cpuset | `cpuset.cpus` | threads cannot run elsewhere; changes the affinity programs see |
| pids | `pids.max`, `pids.events` | `fork()`/`clone()` fails with **`EAGAIN`** |
| io | `io.max`, `io.weight`, `io.stat` | I/O is **delayed** |

And one namespace:

| Namespace | Effect |
|---|---|
| cgroup | `/proc/self/cgroup` and new `cgroup2` mounts are relative to the cgroup the namespace was created in |

## Updated properties table

| Property | After `fork()` | After `execve()` | Change with |
|---|---|---|---|
| cgroup membership | same as parent (or `CLONE_INTO_CGROUP`) | preserved | write PID to `cgroup.procs` |
| cgroup namespace root | same unless `CLONE_NEWCGROUP` | preserved | `unshare`, `setns` |
| Memory already charged | stays with the cgroup that was charged | stays | not moved when the process moves |

## The sentence to remember

> Namespaces decide what a process can see; cgroups decide how much of the
> machine the process's whole group can consume, and what happens when it tries
> to consume more.

## What has *not* been explained yet

- **Privilege.** A root process in a cgroup can still write to
  `/sys/fs/cgroup/.../memory.max` of its own cgroup, unless the mount is
  read-only or its capabilities are dropped. Chapter 05.
- **Syscall filtering.** Chapter 06.
- **The cgroup device controller.** On cgroup v2, device access control is not a
  file-based controller; it is implemented with **eBPF programs** attached to
  the cgroup (`BPF_CGROUP_DEVICE`). runc generates such a program from the OCI
  `devices` list. Chapters 06 and 11 return to this.

## Self-check questions

1. Why can neither a PID namespace nor `RLIMIT_NPROC` stop a fork bomb from
   affecting other containers, while `pids.max` can? (§1, §5)
2. What is the "no internal processes" rule, and how does a Kubernetes pod's
   cgroup layout comply with it? (§2, §7)
3. A container reports memory usage at 95% of its limit but never gets OOM-killed.
   Give the most likely explanation, and the file that proves it. (§3)
4. Explain how 16 threads can be throttled in a cgroup with a 2-CPU quota on an
   idle 16-core machine. (§4)
5. What does a process see in `/proc/self/cgroup` inside a new cgroup namespace,
   and why must the runtime join the cgroup first? (§6)
6. A JVM is killed with exit code 137, but its logs show no `OutOfMemoryError`.
   List three memory areas outside the heap that might explain it. (§7)
7. `docker run --cpus=1.5`: which OCI fields and which cgroup file result, and
   what does `Runtime.availableProcessors()` return? (§7)

## Next: Chapter 05 — Capabilities

A process in namespaces and cgroups may still run as UID 0. Chapter 05 explains
how Linux splits root's power into capabilities, the five capability sets and
their transformation at `execve()`, how capabilities interact with user
namespaces, and why "root inside a container" is not automatically "root on the
host".
