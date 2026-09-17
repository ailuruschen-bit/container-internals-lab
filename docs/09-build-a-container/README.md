# Chapter 09 — Build a Container Manually

## Why this chapter exists

Chapters 01–08 covered every Linux primitive a container uses. This chapter puts
them together: a small container runtime, written in Go, built **one isolation
step at a time**. At each step we run it, observe what changed, and, just as
importantly, state what has **not** changed. The goal is not a production runtime
(that is runc, Chapter 11); it is to make the sentence from Chapter 01 concrete:

> A container is still a Linux process. The kernel gives that process an isolated
> and restricted view of system resources.

By the end you will have a program, `minic`, that creates a process in new
namespaces, a private rootfs entered with `pivot_root`, a cgroup with limits,
a reduced capability set, and a seccomp filter, then `execve()`s a shell, and
you will be able to point at the line responsible for each property.

## Why Go, and the re-exec pattern

Go is used because runc, containerd, and Moby are Go, so this previews their
code. But Go's runtime is multithreaded from the start, and Chapter 03 §4 showed
that some setup (entering a mount namespace, `pivot_root`) is restricted for
multithreaded processes, and Chapter 03 §7 showed user-namespace maps must be
written by the parent while the child waits. The standard trick, used by real Go
runtimes, is **re-exec**: the program runs itself again (`/proc/self/exe`) with a
hidden first argument, so the "child" side starts as a fresh process in the new
namespaces before doing the delicate setup. Section 1 explains it.

## Reading order

| # | Section | Adds | What changes |
|---|---|---|---|
| 1 | [The re-exec skeleton](01-reexec-skeleton.md) | parent/child split via `/proc/self/exe` | nothing yet; the structure |
| 2 | [UTS and PID namespaces](02-uts-pid.md) | `CLONE_NEWUTS`, `CLONE_NEWPID` | own hostname; the shell is PID 1 |
| 3 | [Mount namespace and rootfs](03-mount-rootfs.md) | `CLONE_NEWNS`, `pivot_root`, `/proc` | own `/`, own process view |
| 4 | [Network and IPC namespaces](04-net-ipc.md) | `CLONE_NEWNET`, `CLONE_NEWIPC` | isolated network and IPC |
| 5 | [cgroup limits](05-cgroups.md) | a cgroup with `memory.max`, `pids.max` | bounded resources |
| 6 | [Capabilities and seccomp](06-caps-seccomp.md) | drop caps, `no_new_privs`, seccomp | reduced privilege and syscalls |
| 7 | [The finished mini container](07-finished.md) | user namespace option; review | rootless option; the whole picture |
| 8 | [Chapter summary](08-summary.md) | — | what changed, what did not |

The complete, buildable program is in
[`labs/09-build-a-container/minic/`](../../labs/09-build-a-container/minic/); each
section corresponds to a stage you can check out and run.

## Prerequisites

- **All of Chapters 01–08.** This chapter references them constantly and does not
  re-explain the mechanisms.
- Go 1.22+ and a Linux VM with `sudo`. A rootfs from
  [Chapter 07 Lab 01](../../labs/07-rootfs-chroot-pivot-root/lab-01-build-a-rootfs/)
  at `/tmp/rootfs`.

## Environment

Build on Linux (or cross-compile: `GOOS=linux go build`). Most steps need `sudo`;
section 7 shows the rootless variant with a user namespace.
