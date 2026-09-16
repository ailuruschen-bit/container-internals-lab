# 5. The PIDs and I/O Controllers

## Part 1: The PIDs controller

### The problem

Every process and thread consumes a PID (Chapter 01 §2) and kernel memory. The
PID space is limited (`/proc/sys/kernel/pid_max`), and so is the global number
of tasks (`/proc/sys/kernel/threads-max`). A **fork bomb**, a program that forks
in an endless loop, exhausts them in seconds, after which **no process on the
machine** can start: not a shell, not `sshd` handling a login, not a health
check.

A PID namespace does not help: it gives a container its own *numbering*, but
all tasks still consume the same global kernel resources. `RLIMIT_NPROC` does
not help either: it counts processes **per real user ID across the whole
machine**, so it cannot distinguish two containers both running as UID 0 or
UID 1000.

### The mechanism

| File | Meaning |
|---|---|
| `pids.current` | number of tasks (processes **and threads**) in the cgroup and its descendants |
| `pids.max` | limit; default `max` |
| `pids.peak` | highest `pids.current` observed (recent kernels only) |
| `pids.events` | `max N`: how many times a fork was refused because of the limit |

When a `fork()`/`clone()` would exceed `pids.max` anywhere up the hierarchy,
the call fails with **`EAGAIN`** ("Resource temporarily unavailable"). Nothing
is killed. Existing processes continue; the program trying to create a
process must handle the error.

Note that **threads count**. A JVM with 200 threads uses 200 of the cgroup's
PIDs. An application that fails with
`java.lang.OutOfMemoryError: unable to create native thread` inside a container
may be hitting `pids.max`, not memory.

### Why this matters for containers

- `docker run --pids-limit=100` writes `pids.max`. Kubernetes sets a pod-level
  PID limit through the kubelet setting `podPidsLimit`.
- The PIDs controller is one of the few protections against a container making
  the entire node unusable, and it costs almost nothing.

## Part 2: The I/O controller

### The problem

A process doing heavy disk writes or reads can saturate a block device, making
every other process on the host that touches the same disk slow, including
logging, databases, and the container runtime itself.

### Minimum background: block devices and I/O paths

- A **block device** is identified by `MAJOR:MINOR` numbers, like any device node
  (Chapter 02 §5): `lsblk -o NAME,MAJ:MIN` shows them. The I/O controller
  configures limits per device.
- **Direct I/O** (`O_DIRECT`) goes from the process straight to the device, and
  is easy to attribute to a cgroup.
- **Buffered I/O** (normal `write()`) first copies data into the **page cache**;
  the kernel writes it to disk later, in **writeback** threads. For the I/O
  controller to charge this later I/O to the right cgroup, it must know which
  cgroup dirtied the page. That is only possible in cgroup v2, where the memory
  and I/O controllers share one hierarchy. It requires both controllers to be
  enabled, and a filesystem that supports cgroup writeback (ext4, xfs, and
  btrfs do).

### The mechanism

| File | Format | Meaning |
|---|---|---|
| `io.max` | `MAJ:MIN rbps=N wbps=N riops=N wiops=N` | hard caps in bytes/s and operations/s per device |
| `io.weight` | `default 100` or `MAJ:MIN 200` | proportional share (1–10000), effective with I/O schedulers/cost models that support it (for example BFQ, or `io.cost`) |
| `io.stat` | per device: `rbytes wbytes rios wios dbytes dios` | accounting |
| `io.pressure` | PSI | time stalled on I/O |
| `io.latency` | `MAJ:MIN target=MS` | latency target protection (advanced) |

When a cgroup exceeds `io.max`, the kernel **delays** its I/O requests. As with
`cpu.max`, nothing fails and nothing is killed; the application becomes slower.

A limit applies to a **device**. If a container writes to an overlay filesystem
on `/var/lib/docker`, the limit must be set on the device that holds that
directory, not on a partition the container never touches.

### Why this matters for containers

- `docker run --device-write-bps /dev/sda:10mb` and `--device-read-iops` write
  `io.max`; `--blkio-weight` is translated to a weight file (`io.bfq.weight` or
  `io.weight`, depending on what the kernel provides).
- Kubernetes does not expose I/O limits in the pod spec, but node-level
  components can use `io.weight` and `io.pressure`.
- Throttled buffered writes can surface in unexpected places: a process may
  block in `write()`, or in `fsync()`, or be slowed through memory pressure when
  dirty pages cannot be written out quickly enough.

## Evidence

- Lab: [`lab-04-pids-controller`](../../labs/04-cgroups/lab-04-pids-controller/)
- Lab: [`lab-05-io-controller`](../../labs/04-cgroups/lab-05-io-controller/)

## Further Reading

- Kernel docs: [Control Group v2 — PID](https://docs.kernel.org/admin-guide/cgroup-v2.html#pid)
  and [IO](https://docs.kernel.org/admin-guide/cgroup-v2.html#io), including
  "Writeback" — defines every file and explains why buffered-write attribution
  requires the unified hierarchy.
- Kernel docs: [Process Number Controller (v1)](https://docs.kernel.org/admin-guide/cgroup-v1/pids.html)
  — a short, clear example of how the limit behaves in nested groups (the
  semantics are the same in v2).
- [`getrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html),
  `RLIMIT_NPROC` — read the exact definition to see why it is per user, not per
  group.
- [`fork(2)`](https://man7.org/linux/man-pages/man2/fork.2.html), ERRORS —
  `EAGAIN` conditions, including "the process's cgroup PID limit".
- Kubernetes documentation: [Process ID Limits And Reservations](https://kubernetes.io/docs/concepts/policy/pid-limiting/)
  — pod and node PID limits.
- Kernel docs: [BFQ (Budget Fair Queueing)](https://docs.kernel.org/block/bfq-iosched.html)
  — the I/O scheduler that implements `io.weight` proportional sharing.
