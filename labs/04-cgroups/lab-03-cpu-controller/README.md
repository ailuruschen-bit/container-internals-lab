# Lab 03 — The CPU Controller: Quotas, Throttling, Weights, and cpusets

## Goal

Produce evidence that:

1. `cpu.max` caps CPU **time** per period, regardless of thread count;
2. many threads exhaust the quota quickly and are throttled, visible in
   `cpu.stat`;
3. `cpu.weight` divides CPU proportionally only under contention;
4. `cpu.max` does not change the number of CPUs a program sees, while
   `cpuset.cpus` does.

## Prerequisites

- Linux VM with cgroup v2, `sudo`, `gcc`, and **at least 2 vCPUs** (4 is better).
- Read: [4. The CPU controller](../../../docs/04-cgroups/04-cpu-controller.md).

## Setup

```bash
gcc -Wall -O2 -pthread -o burn burn.c
sudo -i
cd /path/to/this/lab
CG=/sys/fs/cgroup
echo "+cpu +cpuset" > $CG/cgroup.subtree_control
mkdir -p $CG/labcpu $CG/labcpu-a $CG/labcpu-b
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }
nproc
```

## Experiment

### Part A — Baseline without limits

```bash
./burn 1 5
./burn 2 5
```

### Part B — A quota of half a CPU

**Predict first.** With `cpu.max = 50000 100000`, how many CPUs will `burn 1`
use on average? And `burn 2`?

```bash
echo "50000 100000" > $CG/labcpu/cpu.max
incg labcpu ./burn 1 5
incg labcpu ./burn 2 5
cat $CG/labcpu/cpu.stat
```

While a longer run is active, watch it from a second root terminal:

```bash
incg labcpu ./burn 1 20 &
top -b -n 3 -d 2 -p "$(pgrep -n burn)" | grep burn
```

### Part C — Throttling with many threads

```bash
echo "100000 100000" > $CG/labcpu/cpu.max        # 1 CPU
before=$(grep -E '^(nr_periods|nr_throttled|throttled_usec)' $CG/labcpu/cpu.stat)
incg labcpu ./burn "$(nproc)" 5
echo "$before"; grep -E '^(nr_periods|nr_throttled|throttled_usec)' $CG/labcpu/cpu.stat
```

Compute the throttled fraction: `(nr_throttled after − before) / (nr_periods
after − before)`.

Then try a shorter period with the same ratio:

```bash
echo "10000 10000" > $CG/labcpu/cpu.max           # still 1 CPU, 10 ms periods
incg labcpu ./burn "$(nproc)" 5
```

### Part D — Weights only matter under contention

Pin both groups to a single CPU so they must compete:

```bash
echo 0 > $CG/labcpu-a/cpuset.cpus
echo 0 > $CG/labcpu-b/cpuset.cpus
echo 300 > $CG/labcpu-a/cpu.weight
echo 100 > $CG/labcpu-b/cpu.weight
incg labcpu-a ./burn 1 10 &
incg labcpu-b ./burn 1 10 &
wait
```

**Predict first.** What average CPU usage will each report?

Now run only `labcpu-b` alone:

```bash
incg labcpu-b ./burn 1 5
```

### Part E — What a program sees

```bash
incg labcpu nproc
incg labcpu ./burn 1 1 | head -n1
echo 0-1 > $CG/labcpu-a/cpuset.cpus
incg labcpu-a nproc
incg labcpu-a ./burn 1 1 | head -n1
cat $CG/labcpu/cpu.max
```

### Cleanup

```bash
rmdir $CG/labcpu $CG/labcpu-a $CG/labcpu-b
exit
```

## Expected observations

**Part A.** `burn 1` uses about `1.00` CPU; `burn 2` about `2.00`.

**Part B.** Both runs report about `0.50` CPUs used, whatever the thread count.
`top` shows the process at about 50%. `cpu.stat` shows `nr_throttled` close to
`nr_periods`.

**Part C.** With N threads (N = vCPU count) and a 1-CPU quota, average usage is
about `1.00`, and most periods are throttled: the threads consume 100 ms of
quota in about 100/N ms, then wait. With 10 ms periods, average usage is still
about `1.00`, but each throttled interval is shorter (lower worst-case wait).

**Part D.** With both running on CPU 0: `labcpu-a` reports about `0.75`,
`labcpu-b` about `0.25` (300:100). When `labcpu-b` runs alone, it reports about
`1.00` despite its lower weight.

**Part E.** In `labcpu` (quota only), `nproc` and `affinity_cpus` show **all**
CPUs of the VM. In `labcpu-a` with `cpuset.cpus=0-1`, `nproc` prints `2` and
`affinity_cpus=2`, while `online_cpus` still shows the full count.

## Why this happens

- **B, C.** CFS bandwidth control accounts CPU time of all threads of the cgroup
  against one quota per period; when `runtime_remaining` reaches zero, the
  cgroup's runqueue entities are throttled until the period timer refills them.
- **D.** Under contention, the scheduler divides time between sibling group
  entities in proportion to their weights; without contention, there is nothing
  to divide.
- **E.** `nproc` uses `sched_getaffinity()`, which the cpuset controller
  restricts; `cpu.max` does not affect affinity. `sysconf(_SC_NPROCESSORS_ONLN)`
  reports online CPUs of the machine.

## Connection to containers

- Part B/C is `docker run --cpus=...` and Kubernetes `limits.cpu`. The
  throttled fraction in Part C is what container CPU throttling dashboards
  display.
- Part D is Kubernetes `requests.cpu`: a guarantee under contention, not a cap.
- Part E is why a JVM must read `cpu.max` explicitly to size its thread pools;
  `nproc` alone gives the wrong answer in a container with a CPU limit but no
  cpuset.

## Questions to think about

1. A JVM with 16 parallel GC threads runs in a container with `limits.cpu: 2` on
   a 16-core node. Using Part C, describe what happens during a stop-the-world
   GC.
2. Why can a single-threaded program never be throttled by a quota of 1 CPU or
   more?
3. What is the trade-off between long and short CPU periods?
4. Why do many platform teams set CPU requests but not CPU limits for latency-
   sensitive services, while always setting memory limits?
