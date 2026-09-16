# Lab 06 — The cgroup Namespace and the JVM Inside a cgroup

## Goal

Produce evidence that:

1. a cgroup namespace changes how cgroup paths are shown, not membership;
2. a `cgroup2` mount made inside the namespace is rooted at the process's cgroup;
3. the JVM derives heap size, processor count, and GC choice from cgroup files;
4. `-XX:-UseContainerSupport` makes the JVM size itself from the host;
5. a heap limit produces `OutOfMemoryError`, while exceeding `memory.max`
   produces a kernel `SIGKILL` (exit 137) with no shutdown hook.

## Prerequisites

- Linux VM with cgroup v2, `sudo`, `util-linux`, and a JDK 17+ (`java -version`).
- Read: [6. The cgroup namespace](../../../docs/04-cgroups/06-cgroup-namespace.md) and
  [7. Containers, Kubernetes, and the JVM](../../../docs/04-cgroups/07-containers-and-jvm.md).

## Setup

```bash
sudo -i
cd /path/to/this/lab
CG=/sys/fs/cgroup
echo "+cpu +memory +pids" > $CG/cgroup.subtree_control
mkdir -p $CG/labjvm
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }
nproc; free -m | head -n2
```

## Experiment

### Part A — Path virtualization

**Predict first.** Inside the new cgroup namespace, what will
`/proc/self/cgroup` print?

```bash
incg labjvm cat /proc/self/cgroup
incg labjvm unshare --cgroup cat /proc/self/cgroup
```

Create the namespace in the wrong order (before joining the cgroup):

```bash
unshare --cgroup bash -c 'echo $$ > /sys/fs/cgroup/labjvm/cgroup.procs; cat /proc/self/cgroup'
```

### Part B — A cgroup2 mount rooted at the container's cgroup

```bash
echo 512M > $CG/labjvm/memory.max
incg labjvm unshare --cgroup --mount bash -c '
  mount -t cgroup2 none /sys/fs/cgroup
  echo "memory.max seen inside: $(cat /sys/fs/cgroup/memory.max)"
  ls /sys/fs/cgroup | grep -c . '
ls $CG | grep -c .
```

### Part C — What the JVM derives

```bash
java Probe.java                                           # host view
echo 512M > $CG/labjvm/memory.max
echo "150000 100000" > $CG/labjvm/cpu.max                 # 1.5 CPUs
incg labjvm java Probe.java
incg labjvm java -XshowSettings:system -version 2>&1 | head -n 20
```

**Predict first.** With `memory.max = 512M` and `cpu.max = 1.5 CPUs`, what will
`maxMemory()` and `availableProcessors()` print? Which GC?

See exactly which files the JVM read:

```bash
incg labjvm java -Xlog:os+container=trace -version 2>&1 | grep -iE 'path|limit|quota|period|cpu count|memory' | head -n 25
```

### Part D — Turning container support off

```bash
incg labjvm java -XX:-UseContainerSupport Probe.java
```

### Part E — Two different ways to run out of memory

Heap limit reached:

```bash
incg labjvm java -Xmx128m Probe.java heap; echo "exit code: $?"
grep oom_kill $CG/labjvm/memory.events
```

Container limit reached with off-heap memory. Allow large direct buffers so that
the JVM's own limit does not trigger first:

```bash
incg labjvm java -Xmx128m -XX:MaxDirectMemorySize=4g Probe.java direct; echo "exit code: $?"
grep oom_kill $CG/labjvm/memory.events
dmesg | grep -i 'memory cgroup out of memory' | tail -n1
```

**Predict first.** Will the second run print `caught ...` or the shutdown-hook
message?

Finally, see the JVM's own direct-memory limit instead of the kernel's:

```bash
incg labjvm java -Xmx128m Probe.java direct 2>&1 | tail -n 3; echo "exit code: ${PIPESTATUS[0]}"
```

### Cleanup

```bash
echo 1 > $CG/labjvm/cgroup.kill 2>/dev/null; sleep 0.5
rmdir $CG/labjvm
exit
```

## Expected observations

**Part A.** Without the namespace: `0::/labjvm`. Inside: `0::/`. In the wrong
order, the namespace root is the root shell's original cgroup, so the output is
`0::/../labjvm` (or a similar path containing `..`).

**Part B.** `memory.max seen inside: 536870912`, read from
`/sys/fs/cgroup/memory.max` directly. The inside mount lists far fewer entries
than the host's `/sys/fs/cgroup` (no `system.slice`, `user.slice`, ...).

**Part C.** Host view: processors = VM CPU count, heap max = about 25% of VM RAM.
In `labjvm`: `availableProcessors() : 2` (ceil of 1.5), `maxMemory()` about
`128 MiB` (25% of 512 MiB), GC `Copy` (Serial GC's young collector name), because
512 MiB is below the server-class threshold. `-XshowSettings:system` prints
`Provider: cgroupv2`, `Effective CPU Count: 2`, `Memory Limit: 512.00M`. The trace
log mentions `/sys/fs/cgroup/labjvm` (or the namespace root) and `cpu.max` /
`memory.max` values.

**Part D.** The JVM reports the **host's** processor count and a heap based on
host RAM, although the process is still limited by the cgroup.

**Part E.**
- `-Xmx128m ... heap`: allocations stop around 100–128 MiB, `caught
  java.lang.OutOfMemoryError: Java heap space` is printed, the shutdown hook
  message appears, exit code `0`; `oom_kill` is unchanged.
- `... direct` with `MaxDirectMemorySize=4g`: allocations continue past the heap
  size up to roughly 350–450 MiB, then the output stops, the shell prints
  `Killed`, exit code `137`, **no** shutdown hook message; `oom_kill` increased,
  and `dmesg` shows a `Memory cgroup out of memory` line for `java`.
- Without the flag: `java.lang.OutOfMemoryError: Cannot reserve ... direct buffer
  memory` after about 128 MiB (the default direct memory limit equals the max heap
  size), and a non-zero exit code from the uncaught exception (`1`).

## Why this happens

- **A, B.** The namespace root is the cgroup of the creating process at creation
  time; procfs and the cgroup2 mount present paths relative to it.
- **C.** HotSpot's `CgroupV2Subsystem` reads `memory.max` and `cpu.max`,
  computes `ceil(150000/100000) = 2` processors, and applies
  `MaxRAMPercentage=25`. With less than 2 CPUs or ~1792 MB, the JVM is not
  "server class" and selects Serial GC; with 2 CPUs and 512 MB, the memory
  condition applies.
- **E.** The heap limit is enforced by the JVM, which throws a Java exception.
  `memory.max` is enforced by the kernel on the total footprint, which kills the
  process with `SIGKILL`; user-space code cannot react.

## Connection to containers

- Part B is how every modern container sees `/sys/fs/cgroup`.
- Part C is what happens to every Java service deployed with memory and CPU
  limits.
- Part E is the difference between a Java OOM in your logs and a pod restarted
  with reason `OOMKilled` and no logs about it.

## Questions to think about

1. A Spring Boot service in a 1 GiB container uses `-Xmx1g`. It is restarted
   every few hours with exit code 137. Explain why, and propose two changes.
2. A pod has `requests.cpu: 1` and no CPU limit on a 64-core node. How many
   processors does the JVM report? Why can that be a problem, and how could you
   fix it without adding a CPU limit?
3. Why is `-Xlog:os+container=trace` a better first debugging step than reading
   `Runtime.availableProcessors()` in application code?
4. In Part A, why must a runtime move the process into its cgroup *before*
   creating the cgroup namespace?
