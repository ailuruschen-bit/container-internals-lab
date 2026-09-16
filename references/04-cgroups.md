# References — Chapter 04: cgroups v2

Version note: kernel source links are pinned to Linux **v6.12**. JVM behavior is
described for JDK 17–21; version numbers of backports are stated where they
matter.

## Kernel documentation (primary)

| Document | Why read it | Used in |
|---|---|---|
| [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html) | The authoritative reference for the whole chapter: basic operations, delegation, every controller's files, the cgroup namespace, and the v1 rationale. | §1–6 |
| [CFS Bandwidth Control](https://docs.kernel.org/scheduler/sched-bwc.html) | Quota, period, throttling, burst, and multi-threaded behavior. | §4 |
| [CFS Scheduler](https://docs.kernel.org/scheduler/sched-design-CFS.html) | Fair scheduling and group scheduling background. | §4 |
| [PSI — Pressure Stall Information](https://docs.kernel.org/accounting/psi.html) | The `*.pressure` files. | §3–5 |
| [Process Number Controller (v1)](https://docs.kernel.org/admin-guide/cgroup-v1/pids.html) | Clear nested-limit example; same semantics in v2. | §5 |
| [BFQ I/O scheduler](https://docs.kernel.org/block/bfq-iosched.html) | Proportional I/O weights. | §5 |

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`cgroups(7)`](https://man7.org/linux/man-pages/man7/cgroups.7.html) | v1/v2 overview and `/proc/<pid>/cgroup`. | §1–2 |
| [`cgroup_namespaces(7)`](https://man7.org/linux/man-pages/man7/cgroup_namespaces.7.html) | Namespace root, `..` paths, motivation. | §6 |
| [`getrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html) | Why resource limits are per process or per user. | §1, §5 |
| [`clone(2)`](https://man7.org/linux/man-pages/man2/clone.2.html) | `CLONE_INTO_CGROUP`. | §2 |
| [`proc_pid_oom_score_adj(5)`](https://man7.org/linux/man-pages/man5/proc_pid_oom_score_adj.5.html) | OOM victim selection. | §3 |

## Kernel source (primary)

- [`kernel/cgroup/cgroup.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/cgroup/cgroup.c)
  — core: `cgroup_procs_write()`, `cgroup_can_fork()`/`cgroup_post_fork()`,
  `cgroup_kill_write()`, and the no-internal-process checks.
- [`mm/memcontrol.c`](https://elixir.bootlin.com/linux/v6.12/source/mm/memcontrol.c)
  — `try_charge_memcg()`, `mem_cgroup_handle_over_high()`,
  `mem_cgroup_out_of_memory()`.
- [`kernel/sched/fair.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/sched/fair.c)
  — `throttle_cfs_rq()` and `tg_set_cfs_bandwidth()` (in `core.c`) for CPU
  bandwidth.
- [`kernel/cgroup/pids.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/cgroup/pids.c)
  — the entire PIDs controller in a few hundred lines; the best first controller
  to read.
- [`block/blk-throttle.c`](https://elixir.bootlin.com/linux/v6.12/source/block/blk-throttle.c)
  — `io.max` enforcement.

## Container runtimes, Kubernetes, and systemd (primary for §2 and §7)

- OCI Runtime Specification, [config-linux.md: Control groups](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#control-groups).
- runc, [docs/cgroup-v2.md](https://github.com/opencontainers/runc/blob/main/docs/cgroup-v2.md)
  — runc's v2 requirements and OCI-to-v2 conversions.
- [opencontainers/cgroups](https://github.com/opencontainers/cgroups) — the
  cgroup manager library used by runc (previously `libcontainer/cgroups` inside
  the runc repository); Chapter 11 reads it.
- systemd, [Control Group APIs and Delegation](https://systemd.io/CGROUP_DELEGATION/)
  and [`systemd.resource-control(5)`](https://www.freedesktop.org/software/systemd/man/latest/systemd.resource-control.html).
- Kubernetes: [Resource Management for Pods and Containers](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/),
  [Pod QoS Classes](https://kubernetes.io/docs/concepts/workloads/pods/pod-qos/),
  [About cgroup v2](https://kubernetes.io/docs/concepts/architecture/cgroups/),
  [Configuring a cgroup driver](https://kubernetes.io/docs/tasks/administer-cluster/kubeadm/configure-cgroup-driver/),
  [Process ID Limits and Reservations](https://kubernetes.io/docs/concepts/policy/pid-limiting/).
- Docker: [`docker run` reference](https://docs.docker.com/reference/cli/docker/container/run/)
  (`--memory`, `--cpus`, `--pids-limit`, `--cgroupns`).

## JVM (primary for §7)

- OpenJDK HotSpot source:
  [`cgroupSubsystem_linux.cpp`](https://github.com/openjdk/jdk/blob/master/src/hotspot/os/linux/cgroupSubsystem_linux.cpp)
  and [`cgroupV2Subsystem_linux.cpp`](https://github.com/openjdk/jdk/blob/master/src/hotspot/os/linux/cgroupV2Subsystem_linux.cpp).
- [JDK-8230305](https://bugs.openjdk.org/browse/JDK-8230305) — cgroup v2 support.
- [JDK-8281181](https://bugs.openjdk.org/browse/JDK-8281181) — CPU shares no
  longer used for the active processor count.
- Oracle, [Java SE 21 GC Tuning Guide — Ergonomics](https://docs.oracle.com/en/java/javase/21/gctuning/ergonomics.html).

## Secondary sources (selected)

- Dave Chiluk, ["Unthrottled: Fixing CPU Limits in the Cloud"](https://engineering.indeedblog.com/blog/2019/12/unthrottled-fixing-cpu-limits-in-the-cloud/)
  (2019) — real-world CFS throttling investigation.
- Jonathan Corbet, LWN, ["The unified control group hierarchy in 3.16"](https://lwn.net/Articles/601840/)
  (2014) — why v2 has a single hierarchy.
