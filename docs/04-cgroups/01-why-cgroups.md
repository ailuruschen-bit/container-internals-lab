# 1. Why cgroups Exist

## The problem: resources are consumed by groups of processes

A web application is rarely one process. A JVM has dozens of threads; a
PostgreSQL server forks a process per connection; a CI job runs a shell that
starts compilers, test runners, and browsers. When an operator says "this
application may use 2 CPUs and 4 GiB of memory", the limit is about the
**application as a group**, including processes that do not exist yet.

Linux needed a way to:

1. **group** arbitrary processes, with new children joining their parent's group
   automatically;
2. **account** for the resources the group uses (CPU time, memory, I/O,
   number of processes);
3. **limit** or **prioritize** that usage;
4. do this **hierarchically**, so that a machine can be divided among services,
   and a service further among its parts.

## Why namespaces cannot do this

A namespace changes the result of lookups: which PID a number refers to, which
mount a path crosses, which interface a packet leaves through. None of that
involves counting consumption. The scheduler, the page allocator, and the block
layer do not consult namespaces when deciding whether a task may run, allocate a
page, or submit I/O. Chapter 03 Lab 08 showed a process in six namespaces with
the same `nproc` and `free` output as the host.

## Why `setrlimit()` is not enough

Linux has had **resource limits** since the beginning (`ulimit` in the shell,
`setrlimit()` in C, `/proc/<pid>/limits`). They are inherited across `fork()`
and preserved across `execve()` (Chapter 01 §8). But:

| Resource limit | Limitation |
|---|---|
| `RLIMIT_AS` (address space) | limits *virtual* memory per process; a JVM reserves far more virtual memory than it uses, and 10 processes each get the full limit |
| `RLIMIT_CPU` | total CPU **seconds** per process, then `SIGXCPU`/`SIGKILL`; not a rate such as "2 CPUs" |
| `RLIMIT_NPROC` | counts processes **per user ID**, across the whole machine, not per application |
| (none) | no limit for page cache, kernel memory, network buffers, or disk I/O rate |

Resource limits are **per process** (or per user), **static**, and mostly about
**totals**, while operators need **per group**, **dynamic**, **rate-based**
control.

## The Linux abstraction: control groups

A **control group** is a set of processes, arranged in a tree, with settings
and statistics attached. Three terms recur in this chapter:

| Term | Meaning |
|---|---|
| **cgroup** | one node in the tree; a set of processes |
| **hierarchy** | the tree of cgroups, exposed as a filesystem |
| **controller** (also "subsystem") | a kernel component that accounts for and controls one resource type for cgroups: `cpu`, `memory`, `pids`, `io`, `cpuset`, `hugetlb`, `rdma`, `misc` |

The kernel hooks each controller into the code that actually hands out the
resource:

```text
   scheduler picks next task ──► cpu controller: has this cgroup used its quota?
   page allocation / charge   ──► memory controller: over memory.max? reclaim or OOM
   fork() / clone()           ──► pids controller: over pids.max? fail with EAGAIN
   block I/O submission       ──► io controller: over io.max? delay the request
```

This is the key difference from namespaces: cgroups sit **in the allocation
paths** of the kernel.

## A short history: v1 and v2

cgroups were merged in Linux 2.6.24 (2008), originally developed at Google as
"process containers". The first design, now called **cgroup v1**, allowed a
**separate hierarchy per controller**: processes could be in one tree for CPU
and a different tree for memory. This flexibility made coordination between
controllers (for example, attributing page-cache writeback I/O to the cgroup
whose memory it dirtied) nearly impossible, and interfaces were inconsistent
between controllers.

**cgroup v2** (stable since Linux 4.5, 2016) has a **single unified hierarchy**:
every process belongs to exactly one cgroup, and all enabled controllers apply
to that same tree. Interfaces follow common conventions (`*.max`, `*.current`,
`*.stat`, `*.events`). Pressure information (PSI) and safe delegation to
unprivileged users were built for v2.

This repository uses v2 only. You will still meet v1 in older systems and in
older runtime code paths (runc has both `fs` and `fs2` managers), and the JVM
contains code for both.

## Why this matters for containers

- Namespaces **plus** cgroups are the two halves of a container's isolation:
  a private view, and a bounded share of resources.
- A container engine creates one cgroup per container (or per pod, with child
  cgroups per container), writes the limits, and places the container's first
  process into it **before** `execve()`. From then on, every child the
  application creates is automatically in the same cgroup.
- Without cgroups, one container could starve every other container on the
  host, which is why a PID namespace without a `pids` limit is still vulnerable
  to a fork bomb.

## Evidence

This section is conceptual; its claims are demonstrated in the labs of sections
2–5, especially [`lab-04-pids-controller`](../../labs/04-cgroups/lab-04-pids-controller/),
which compares `RLIMIT_NPROC` with `pids.max`.

## Further Reading

- Kernel docs: [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html),
  sections "Introduction" and "Issues with v1 and Rationales for v2" (at the
  end). The authoritative document for this whole chapter, written by the cgroup
  maintainer; the rationale section explains exactly why v1 was replaced.
- [`cgroups(7)`](https://man7.org/linux/man-pages/man7/cgroups.7.html) —
  concise overview of v1 and v2, history, and `/proc` files.
- [`getrlimit(2)`](https://man7.org/linux/man-pages/man2/getrlimit.2.html) —
  the resource limits compared in this section, with their precise semantics.
- LWN, Jonathan Corbet, ["The unified control group hierarchy in 3.16"](https://lwn.net/Articles/601840/)
  (2014) — historical context on why a single hierarchy was chosen.
