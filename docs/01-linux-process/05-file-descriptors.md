# 5. File Descriptors

## The problem: how does a process refer to an open file?

Opening a file involves a path lookup, permission checks, and allocating
kernel state such as the current read position. A process should do that
once, then refer to the result cheaply in every later `read()` and `write()`.
The kernel also needs to know which open files belong to which process, so it
can clean them up when the process exits.

## The Linux abstraction: a small integer

`open()` returns a **file descriptor (fd)**: a small non-negative integer that
is an index into the process's **file descriptor table**. Later syscalls take
that integer instead of a path. The kernel always assigns the **lowest unused
number**.

By convention, three descriptors are already open when a program starts:

| fd | Name | Typical use |
|---|---|---|
| 0 | standard input (stdin) | read input |
| 1 | standard output (stdout) | normal output |
| 2 | standard error (stderr) | diagnostics |

Nothing in the kernel makes 0, 1, and 2 special. They are simply inherited
from the parent, which is how a shell connects them to your terminal, a pipe,
or a file.

## Three layers, not one

The most common misunderstanding about file descriptors is thinking the
number points directly to a file. There are three layers:

```text
 process A fd table          open file descriptions           inodes
 ┌────┬──────────┐          (kernel: struct file)          (the file itself)
 │ 0  │  ────────┼──┐
 │ 1  │  ────────┼──┼──►  ┌────────────────────────┐
 │ 3  │  ────────┼──┐     │ offset=120, O_RDONLY   │──►  /var/log/app.log
 └────┴──────────┘  │     └────────────────────────┘        ▲
                    │                                        │
 process B fd table │     ┌────────────────────────┐        │
 (child after fork) │     │ offset=0,  O_WRONLY    │────────┘
 ┌────┬──────────┐  │     └────────────────────────┘
 │ 3  │  ────────┼──┘                 ▲
 │ 4  │  ────────┼────────────────────┘   (a separate open() of the same file)
 └────┴──────────┘
```

1. **File descriptor table** — per process (unless shared with `CLONE_FILES`).
   Maps numbers to open file descriptions. Each entry also carries one flag,
   `FD_CLOEXEC`.
2. **Open file description** — a kernel object (`struct file`) created by each
   successful `open()`. It holds the **file offset** and the status flags
   (`O_RDONLY`, `O_APPEND`, `O_NONBLOCK`...). Several fds, possibly in several
   processes, can point to the same one.
3. **inode** — the underlying file, device, pipe, or socket.

The layers explain behavior that otherwise looks strange:

- After `fork()`, the child gets a **copy of the table**, but both tables point
  to the **same** open file descriptions. If the child reads 10 bytes, the
  parent's next `read()` starts at byte 10, because the offset is shared.
- `dup2(3, 1)` makes entry 1 point to the same open file description as entry 3.
  This is how shell redirection works (Lab 03).
- Two separate `open()` calls on the same path create two open file
  descriptions with independent offsets.

## Not just files

Almost every kernel object a process can hold is represented by a file
descriptor: regular files, directories, terminals, pipes, sockets,
`epoll` instances, timers (`timerfd`), processes (`pidfd`), and, most
importantly for this repository, **namespaces**. Opening
`/proc/<pid>/ns/net` gives you a file descriptor that refers to a network
namespace, and `setns()` accepts that descriptor to join it (Chapter 03).

This is why "everything is a file" matters in practice: the same inheritance
and permission rules apply to all of these objects.

## Inheritance across execve(): close-on-exec

[Section 3](03-fork-exec-clone.md) showed that file descriptors survive
`execve()`. That is useful for stdin, stdout, and stderr, but dangerous for
everything else. Suppose a privileged daemon has a descriptor open to a
sensitive directory or socket, and then starts an untrusted program. The
program inherits the descriptor and can use it, even though it could never
have opened that path itself. A file descriptor is a **capability** in the
general sense: holding it grants access, and permissions were checked only at
`open()` time.

The protection is the **close-on-exec** flag:

- set at creation with `O_CLOEXEC` (`open`), `SOCK_CLOEXEC` (`socket`), etc.;
- or later with `fcntl(fd, F_SETFD, FD_CLOEXEC)`.

During `execve()`, the kernel closes every descriptor that has the flag. Note
that the flag belongs to the **table entry**, not the open file description, so
`dup()`ed copies do not inherit it.

Modern libraries set close-on-exec by default: the JVM, Go's `os` and `net`
packages, and most of glibc's internal opens. Descriptors that should be
inherited must be passed deliberately.

## Limits

Each process has a maximum number of open descriptors, the `RLIMIT_NOFILE`
resource limit (`ulimit -n`, visible in `/proc/<pid>/limits`). Like other
resource limits, it is inherited across `fork()` and preserved across
`execve()`. A JVM server with many sockets can hit this limit and fail with
`Too many open files` (`EMFILE`). Container runtimes set this limit for the
container process explicitly.

## Why this matters for containers

- The container's stdin, stdout, and stderr are just fds 0, 1, and 2 that the
  runtime prepared before `execve()`. They usually point to pipes or a
  pseudo-terminal owned by a shim process, which is how logs are collected.
- A runtime must make sure that **no unintended host descriptors leak** into the
  container. A leaked descriptor bypasses mount namespaces and root filesystem
  changes, because it refers to an already-open object. This is not theoretical:
  CVE-2024-21626 in runc ("Leaky Vessels") was caused by a leaked descriptor
  to a host directory, which let a container process reach the host
  filesystem through `/proc/self/fd/<n>`.
- Namespace file descriptors are the mechanism for entering an existing
  container (`nsenter`, `docker exec`), as Chapter 03 will show.

## Evidence

Lab: [`lab-06-file-descriptors`](../../labs/01-linux-process/lab-06-file-descriptors/)

## Further Reading

- [`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html), sections
  "Open file descriptions" and the `O_CLOEXEC` entry — the authoritative
  description of the three-layer model and why the flag must be atomic.
- [`dup(2)`](https://man7.org/linux/man-pages/man2/dup.2.html) — short;
  explains exactly what is shared between duplicated descriptors.
- [`fcntl(2)`](https://man7.org/linux/man-pages/man2/fcntl.2.html),
  "File descriptor flags" vs "File status flags" — the distinction between
  per-descriptor and per-open-file-description flags.
- [Snyk: Leaky Vessels (CVE-2024-21626) write-up](https://snyk.io/blog/leaky-vessels-docker-runc-container-breakout-vulnerabilities/)
  and the [runc security advisory GHSA-xr7r-f8xq-vfvv](https://github.com/opencontainers/runc/security/advisories/GHSA-xr7r-f8xq-vfvv)
  — a real container breakout explained entirely in terms of this section.
  Read the advisory after the lab.
