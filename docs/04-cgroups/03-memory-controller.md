# 3. The Memory Controller

## The problem

Memory is the resource whose exhaustion hurts most. When a machine runs out of
memory, the kernel's **OOM killer** chooses a victim process somewhere on the
machine and kills it with `SIGKILL`. Without cgroups, one memory-hungry
application can cause an unrelated database to be killed.

The memory controller lets the kernel **account** memory per cgroup, **limit**
it, and handle exhaustion **inside the cgroup** instead of machine-wide.

## Minimum background: what "memory" means to the kernel

A process's memory usage is not one number. The kernel distinguishes, among
others:

| Kind | What it is | Can the kernel free it under pressure? |
|---|---|---|
| **Anonymous memory** (`anon`) | heap, stacks, `malloc`, the Java heap once touched | only by swapping it out |
| **Page cache** (`file`) | cached contents of files that were read or written | yes: clean pages can be dropped; dirty pages must be written first |
| **Shared memory / tmpfs** (`shmem`) | tmpfs files, `/dev/shm`, shared anonymous mappings | only by swapping |
| **Kernel memory** | socket buffers, dentries/inodes (`slab`), page tables, kernel stacks | partially (caches) |

Two more concepts are needed:

- **Virtual vs resident memory.** A process can *reserve* a large virtual
  address range (a JVM reserves the full `-Xmx` heap at startup) without using
  physical pages. Physical pages are allocated only when memory is first
  **touched**. cgroups count **physical** memory that is actually charged, not
  virtual reservations.
- **Reclaim.** When memory is short, the kernel tries to **reclaim** pages:
  drop clean page cache, write dirty pages and then drop them, or swap
  anonymous pages. Only if reclaim fails does it invoke the OOM killer.

## Accounting: who is charged?

Each physical page is **charged** to exactly one cgroup: the cgroup of the
process that first caused it to be allocated. Consequences:

- **Page cache counts.** A container that reads a 5 GiB log file fills page cache
  charged to its cgroup. `memory.current` can be close to the limit even though
  the application's heap is small. This is normal: cache is reclaimable.
- **tmpfs counts.** Files written to a tmpfs (`/dev/shm`, an in-memory
  `emptyDir`) are charged to the writer and cannot be dropped without swap.
- **The first toucher pays.** If a file is cached by one cgroup and later read by
  another, the charge stays with the first one. Moving a process does not move
  its old charges.
- **Kernel memory is included** in v2: socket buffers, dentries, and page tables
  count toward the same limit.

## Interface files

| File | Meaning |
|---|---|
| `memory.current` | total memory currently charged to the cgroup and its descendants |
| `memory.max` | **hard limit**. If usage would exceed it and reclaim fails, the **cgroup OOM killer** runs. Default `max` (unlimited) |
| `memory.high` | **throttle limit**. Above it, allocating processes are forced into reclaim and slowed down, but not killed |
| `memory.low`, `memory.min` | protections: memory the cgroup should keep (best effort / guaranteed) when *other* cgroups cause pressure |
| `memory.swap.max`, `memory.swap.current` | limit and usage of swap |
| `memory.peak` | highest `memory.current` observed (Linux 5.19+) |
| `memory.stat` | breakdown: `anon`, `file`, `shmem`, `kernel`, `sock`, `slab`, `file_dirty`, `pgfault`, ... |
| `memory.events` | counters: `low`, `high`, `max`, `oom`, `oom_kill`, `oom_group_kill` |
| `memory.oom.group` | `1` = when the OOM killer picks a victim in this cgroup, kill **all** its processes |
| `memory.pressure` | PSI: how much time tasks were stalled waiting for memory |

Values accept suffixes: `echo 256M > memory.max`.

## What happens as usage grows

```text
usage below memory.high              normal allocation
        │
usage above memory.high              allocating task reclaims its own cgroup and is
        │                            throttled (sleeps); memory.events "high" increments
        │
usage reaches memory.max             kernel reclaims from this cgroup (drop cache,
        │                            write dirty pages, swap if allowed)
        │
reclaim cannot free enough           cgroup OOM: memory.events "oom" increments;
        │                            the OOM killer selects a task IN THIS CGROUP
        ▼
victim receives SIGKILL              memory.events "oom_kill" increments;
                                     the kernel log says "Memory cgroup out of memory"
```

The OOM killer chooses the victim by a badness score based mainly on its memory
usage, adjusted by `/proc/<pid>/oom_score_adj` (range -1000 to 1000; -1000 means
"never kill"). Only tasks in the cgroup that hit its limit are candidates.

## Why this matters for containers

- `docker run --memory=512m` writes `memory.max`; `--memory-swap` controls
  `memory.swap.max`; `--memory-reservation` maps to `memory.low` (Docker) and
  `memory.high` is set by some runtimes and by Kubernetes' Memory QoS feature.
- **Exit code 137 + `OOMKilled: true`** in `docker inspect` or Kubernetes status
  is the story above: the container reached `memory.max`, reclaim failed, and
  the kernel sent `SIGKILL` (128 + 9). The runtime detects it through
  `memory.events` (`oom_kill`).
- Kubernetes sets `memory.oom.group=1` for containers on cgroup v2 (since 1.28),
  so an OOM kill takes down the whole container, not only one worker process.
- Page cache counting explains why a container's "memory usage" metric may look
  close to its limit without any risk, and why monitoring tools subtract
  inactive file cache (`working set`).
- **For Java:** the JVM's footprint is heap **plus** metaspace, code cache,
  thread stacks, GC data structures, direct buffers, and native allocations by
  libraries. If `-Xmx` is set close to `memory.max`, the heap never throws
  `OutOfMemoryError`, but the **kernel** kills the JVM with `SIGKILL` when the
  total crosses the limit, with no heap dump and no shutdown hooks
  (Chapter 01 §6). Section 7 returns to this.

## Evidence

Lab: [`lab-02-memory-controller`](../../labs/04-cgroups/lab-02-memory-controller/)

## Further Reading

- Kernel docs: [Control Group v2 — Memory](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory)
  — the definitive description of every `memory.*` file, of `memory.high`
  throttling, and of the "Memory Ownership" rules (first-toucher charging).
- Kernel docs: [Memory Resource Controller (v1)](https://docs.kernel.org/admin-guide/cgroup-v1/memory.html)
  — historical, but its "Accounting" section clarifies what page cache charging
  means; use it only as background.
- Kernel docs: [PSI — Pressure Stall Information](https://docs.kernel.org/accounting/psi.html)
  — how `memory.pressure` is computed and why it is a better signal than usage.
- [`proc_pid_oom_score_adj(5)`](https://man7.org/linux/man-pages/man5/proc_pid_oom_score_adj.5.html)
  — how the OOM victim score is adjusted.
- Kubernetes documentation: [Resource Management for Pods and Containers](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/)
  — how memory requests and limits become cgroup settings, and what happens on
  exceeding them.
- Kernel source: [`mm/memcontrol.c`](https://elixir.bootlin.com/linux/v6.12/source/mm/memcontrol.c),
  functions `try_charge_memcg()` and `mem_cgroup_out_of_memory()` — where a
  charge fails, reclaim is attempted, and the cgroup OOM killer is invoked.
