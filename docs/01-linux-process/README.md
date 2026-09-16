# Chapter 01 — Linux Process Fundamentals

## Why this chapter exists

Every container you have ever started was a Linux process. Not a small virtual
machine, not a special kind of object: a process, created with the same system
calls that create your shell, your JVM, and `ls`.

What makes it a "container" is that the kernel was asked, at creation time or
shortly after, to give that process a different *view* of the system and a
restricted set of *permissions and resources*. Those requests are made through
process-related system calls, especially `clone()`, `unshare()`, `setns()`,
and `execve()`.

So before we can talk about namespaces or cgroups, we need a precise model of:

- what the kernel stores about a process;
- how a program asks the kernel to do something (system calls);
- how a new process comes into existence (`fork()` / `clone()`);
- how a process starts running a different program (`execve()`);
- which properties a process *inherits* from its parent and which it *keeps*
  across `execve()`.

The last point is the one that matters most for containers. A container
runtime sets up properties in a process (namespaces, mounts, cgroup
membership, credentials, capabilities, seccomp filters) and then calls
`execve()` to run your application. Your application only works inside the
container because those properties **survive** `execve()`.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [Processes and system calls](01-processes-and-system-calls.md) | A process is a kernel data structure; programs talk to the kernel only through system calls. |
| 2 | [The process tree: PID, PPID, PID 1](02-process-tree.md) | Every process has a parent; PID 1 has special duties. |
| 3 | [fork, execve, and clone](03-fork-exec-clone.md) | Creating a process and running a program are two separate operations. |
| 4 | [/proc: the kernel's view of a process](04-proc-filesystem.md) | You can inspect kernel process state as files. |
| 5 | [File descriptors](05-file-descriptors.md) | Open files are per-process kernel objects that are inherited. |
| 6 | [Signals](06-signals.md) | How the kernel and other processes interrupt a process. |
| 7 | [Credentials: UID, GID, and privilege](07-credentials.md) | Who a process is, from the kernel's point of view. |
| 8 | [Chapter summary: what a container runtime inherits](08-summary.md) | The properties that later chapters will modify. |

Labs for this chapter: [`labs/01-linux-process/`](../../labs/01-linux-process/).
Each section ends with a pointer to the lab that provides evidence for its
claims. Annotated references: [`references/01-linux-process.md`](../../references/01-linux-process.md).

## Prerequisites

- Comfortable in a Linux shell.
- A general idea of what an operating system does.
- Able to read short C programs. Every C construct that matters is explained
  where it is used. You do not need to write C.

## Environment

All labs require a Linux machine (a VM is fine). See the root
[README](../../README.md#environment). Examples in this chapter were written
against Linux 6.x with glibc 2.35+; output formats may vary slightly on other
versions.
