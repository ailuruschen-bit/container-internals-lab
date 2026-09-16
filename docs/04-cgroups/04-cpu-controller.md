# 4. The CPU Controller

## The problem

CPU time is shared by the scheduler among runnable threads. Without cgroups,
the scheduler is fair **per thread**: a program with 100 busy threads gets
roughly 100 times more CPU than a program with one. Operators need two
different kinds of control over groups:

- **proportional sharing**: when the machine is busy, service A should get twice
  as much CPU as service B, but either may use idle CPU;
- **hard caps**: this batch job may never use more than 2 CPUs' worth of time,
  even if the machine is idle (for predictability or billing).

## Minimum background: the scheduler

Linux's normal scheduling class (CFS, and since Linux 6.6 its successor EEVDF)
tracks how much CPU time each runnable entity has received and picks the one
that is most "behind", weighted by priority (`nice` value). With the CPU
controller enabled, the scheduler becomes **hierarchical**: a cgroup is a
scheduling entity that competes with its siblings, and the threads inside it
compete for the cgroup's share.

## Proportional shares: `cpu.weight`

| File | Range | Default |
|---|---|---|
| `cpu.weight` | 1 – 10000 | 100 |
| `cpu.weight.nice` | nice-value equivalent (-20 – 19) | 0 |

When siblings compete for CPU, each receives time in proportion to its weight:
two busy sibling cgroups with weights 200 and 100 get about 2/3 and 1/3 of the
contested CPUs. Weights are **work-conserving**: if one cgroup is idle, the
other may use all the CPU. Weights say nothing when there is no contention.

## Hard caps: `cpu.max` (CFS bandwidth control)

```text
cpu.max:   $QUOTA $PERIOD      (microseconds)   default: "max 100000"
```

In every **period** (default 100 ms), all threads of the cgroup together may run
for at most **quota** microseconds of CPU time. When the quota is used up, every
thread in the cgroup is **throttled** (not scheduled) until the next period
starts.

| `cpu.max` | Meaning |
|---|---|
| `max 100000` | no limit |
| `50000 100000` | 0.5 CPU: 50 ms per 100 ms |
| `200000 100000` | 2 CPUs: 200 ms of CPU time per 100 ms of wall time (possible with ≥2 threads on ≥2 cores) |

Quota is CPU **time**, not a number of cores. A cgroup with quota for "2 CPUs"
on a 16-core machine can still run 16 threads in parallel; it will simply
exhaust 200 ms of quota in 12.5 ms of wall time and then be throttled for the
remaining 87.5 ms of the period.

```text
16 threads, cpu.max = "200000 100000"

period 1 (100 ms wall time)
|████ 12.5 ms: 16 threads run, 200 ms of CPU time used ████|........ throttled 87.5 ms ........|
period 2
|████|........................ throttled ......................|
```

For latency-sensitive, highly parallel software, this means **tail-latency
spikes**: a request arriving during the throttled part waits until the next
period, even though the machine may be idle. `cpu.max.burst` (Linux 5.14+)
allows limited accumulation of unused quota to smooth this.

### Observing throttling: `cpu.stat`

```text
usage_usec 123456789        total CPU time used by the cgroup
user_usec / system_usec
nr_periods 5000             periods in which the cgroup had runnable threads
nr_throttled 1200           periods in which it hit the quota
throttled_usec 45678901     total time threads spent throttled
```

A high `nr_throttled / nr_periods` ratio is the primary signal that a CPU limit
is too low for the workload's parallelism.

`cpu.pressure` (PSI) reports how much time runnable tasks were waiting for CPU,
including waiting caused by throttling.

## Pinning: the `cpuset` controller

`cpuset.cpus` restricts which CPUs the cgroup's threads may run on (and
`cpuset.mems` which NUMA memory nodes). Unlike `cpu.max`, this changes what
`sched_getaffinity()` returns, so programs that ask "how many CPUs can I use?"
see the smaller number. `docker run --cpuset-cpus=0,1` uses it.

## Why this matters for containers

| Setting | cgroup v2 file |
|---|---|
| `docker run --cpus=1.5` | `cpu.max = 150000 100000` |
| `docker run --cpu-shares=512` | `cpu.weight` (converted from the v1 shares scale, 2–262144, to 1–10000) |
| `docker run --cpuset-cpus=0-3` | `cpuset.cpus = 0-3` |
| Kubernetes `limits.cpu: 500m` | `cpu.max = 50000 100000` |
| Kubernetes `requests.cpu: 250m` | `cpu.weight`, proportional to the request |

Two consequences that affect Java services in particular:

- **CPU limits do not hide CPUs.** Inside a container with `--cpus=2` on a
  32-core host, `/proc/cpuinfo` and `sched_getaffinity()` still show 32 CPUs.
  Programs that size thread pools from "number of CPUs" create too many threads
  unless they read `cpu.max`. Modern JVMs do (section 7).
- **Throttling, not slowness.** A multithreaded JVM with many GC threads can burn
  its quota early in each period, especially during garbage collection, causing
  pauses that look like application latency. `cpu.stat` shows it.

Many operators therefore set CPU **requests** (weights) and avoid strict CPU
**limits** (quotas) for latency-sensitive services; this is a trade-off between
isolation and latency, and knowing the mechanism is what lets you make it.

## Evidence

Lab: [`lab-03-cpu-controller`](../../labs/04-cgroups/lab-03-cpu-controller/)

## Further Reading

- Kernel docs: [Control Group v2 — CPU](https://docs.kernel.org/admin-guide/cgroup-v2.html#cpu)
  and [Cpuset](https://docs.kernel.org/admin-guide/cgroup-v2.html#cpuset) —
  definitions of `cpu.weight`, `cpu.max`, `cpu.max.burst`, `cpu.stat`.
- Kernel docs: [CFS Bandwidth Control](https://docs.kernel.org/scheduler/sched-bwc.html)
  — how quota, period, throttling, and burst work, including the
  multi-threaded behavior shown in the diagram. Essential reading for anyone who
  sets CPU limits.
- Kernel docs: [CFS Scheduler](https://docs.kernel.org/scheduler/sched-design-CFS.html)
  — background on fair scheduling and group scheduling.
- Kubernetes documentation: [Resource Management for Pods and Containers — CPU](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/#meaning-of-cpu)
  and the section on how requests and limits are applied.
- Dave Chiluk, ["Unthrottled: Fixing CPU Limits in the Cloud"](https://engineering.indeedblog.com/blog/2019/12/unthrottled-fixing-cpu-limits-in-the-cloud/)
  (Indeed Engineering, 2019) — a detailed investigation of a real CFS throttling
  bug affecting containerized services, and a good example of reading
  `cpu.stat` in production.
