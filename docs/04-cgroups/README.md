# Chapter 04 — Control Groups (cgroups v2)

## Why this chapter exists

Chapter 03 ended with an uncomfortable table: a process inside six new
namespaces can still use every CPU, all memory, unlimited disk I/O, and create
processes until the machine's PID space is exhausted. Namespaces change what a
process **sees**; they do not change what it **consumes**.

Linux solves resource control with **control groups (cgroups)**: a kernel
mechanism that organizes processes into a hierarchy of groups and applies
**accounting** and **limits** to each group through **controllers**. Every
`docker run --memory`, `--cpus`, and `--pids-limit`, and every Kubernetes
`resources.limits`, becomes a write to a file in the cgroup filesystem. The JVM
reads the same files to decide its heap size and thread counts.

This chapter focuses on **cgroups v2**, the unified hierarchy that modern
distributions, systemd, containerd, runc, and Kubernetes use by default.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [Why cgroups exist](01-why-cgroups.md) | Namespaces and `setrlimit` cannot control a group's resources. |
| 2 | [The cgroup v2 hierarchy and filesystem](02-hierarchy-and-cgroupfs.md) | Directories are groups; files are the interface; processes are members. |
| 3 | [The memory controller](03-memory-controller.md) | Accounting, `memory.max`, `memory.high`, and the cgroup OOM killer. |
| 4 | [The CPU controller](04-cpu-controller.md) | Weights vs quotas; CFS bandwidth throttling. |
| 5 | [The PIDs and I/O controllers](05-pids-and-io-controllers.md) | Fork bombs and block I/O limits. |
| 6 | [The cgroup namespace](06-cgroup-namespace.md) | Virtualizing the view of the hierarchy. |
| 7 | [Containers, Kubernetes, and the JVM](07-containers-and-jvm.md) | How runtimes map settings to cgroup files, and how the JVM reads them. |
| 8 | [Chapter summary](08-summary.md) | What changed, what did not. |

Labs: [`labs/04-cgroups/`](../../labs/04-cgroups/).
References: [`references/04-cgroups.md`](../../references/04-cgroups.md).

## Prerequisites

- [Chapter 01](../01-linux-process/): processes, `fork()` inheritance, signals
  (`SIGKILL`, exit code 137).
- [Chapter 02](../02-linux-filesystem/): mounts and pseudo-filesystems; tmpfs.
- [Chapter 03](../03-namespaces/): namespaces in general (for §6).

## Environment

A Linux VM booted with **cgroup v2 only** (the default on Ubuntu 21.10+,
Debian 11+, Fedora 31+, RHEL 9+). Check with:

```bash
stat -fc %T /sys/fs/cgroup     # must print: cgroup2fs
```

All labs need `sudo`. They create cgroups under `/sys/fs/cgroup/lab*` and remove
them at the end. Lab 06 additionally needs a JDK (17 or newer).
