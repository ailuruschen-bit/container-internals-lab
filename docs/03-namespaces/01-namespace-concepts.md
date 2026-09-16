# 1. What a Namespace Is

## The problem: global resources

Before namespaces, several kernel resources existed exactly once per machine:

| Global resource | Where a process meets it | Consequence of being global |
|---|---|---|
| Hostname | `uname()`, `sethostname()` | changing it affects every program |
| Process ID space | `getpid()`, `kill()`, `/proc` | every process can see (and try to signal) every other |
| Mount tree | every path lookup | a mount is visible to all processes |
| System V IPC objects, POSIX message queues | `shmget()`, `msgget()`, `mq_open()` | objects can collide or be attached by anyone with permission |
| Network stack | interfaces, IP addresses, routes, firewall rules, ports | two programs cannot both bind port 8080 |
| UID/GID values and root privilege | credentials, capability checks | UID 0 is the same UID 0 everywhere |
| cgroup hierarchy view | `/proc/self/cgroup` | a process sees its full host cgroup path |
| Monotonic and boot clocks | `clock_gettime()` | cannot be shifted per process |

The early answer to "run several isolated environments on one machine" was a
virtual machine, which duplicates the whole kernel. Namespaces take a different
approach: keep **one kernel**, but give each group of processes its own
**instance** of each global resource.

## The Linux abstraction: a namespace

> A **namespace** wraps a global system resource in an abstraction that makes
> it appear to the processes within the namespace that they have their own
> isolated instance of the resource. — paraphrasing `namespaces(7)`

Three properties define a namespace:

1. **Every process is in exactly one namespace of each type**, all the time.
   On a host without containers, nearly all processes share the *initial*
   namespaces created at boot.
2. **Membership is inherited.** A child created by `fork()`/`clone()` is in the
   same namespaces as its parent unless a `CLONE_NEW*` flag says otherwise.
   Membership is preserved across `execve()` (Chapter 01 §3).
3. **The kernel consults the namespace when answering a syscall.** `uname()`
   returns the hostname of the caller's UTS namespace; `getpid()` returns the
   PID in the caller's PID namespace; path lookup uses the mount tree of the
   caller's mount namespace.

## The namespace types

| Namespace | Flag | Isolates | Linux version |
|---|---|---|---|
| Mount | `CLONE_NEWNS` | mount tree | 2.4.19 (the first; hence the generic name "NS") |
| UTS | `CLONE_NEWUTS` | hostname and NIS domain name | 2.6.19 |
| IPC | `CLONE_NEWIPC` | System V IPC, POSIX message queues | 2.6.19 |
| PID | `CLONE_NEWPID` | process ID number space | 2.6.24 |
| Network | `CLONE_NEWNET` | network devices, stacks, ports, firewall rules | 2.6.29 |
| User | `CLONE_NEWUSER` | user and group IDs, capabilities | 3.8 |
| Cgroup | `CLONE_NEWCGROUP` | view of the cgroup hierarchy | 4.6 |
| Time | `CLONE_NEWTIME` | `CLOCK_MONOTONIC` and `CLOCK_BOOTTIME` offsets | 5.6 |

The name "UTS" comes from `struct utsname`, the structure returned by
`uname()` ("UNIX Time-sharing System").

## Where namespaces live in the kernel

Recall `task_struct` from Chapter 01 §1. Most namespace memberships are grouped
in one structure:

```text
struct task_struct
 ├── nsproxy ──► struct nsproxy
 │                ├── uts_ns               → struct uts_namespace   (hostname)
 │                ├── ipc_ns               → struct ipc_namespace
 │                ├── mnt_ns               → struct mnt_namespace   (mount tree)
 │                ├── pid_ns_for_children  → struct pid_namespace
 │                ├── net_ns               → struct net
 │                ├── time_ns, time_ns_for_children
 │                └── cgroup_ns
 └── cred ──► struct cred
                  └── user_ns              → struct user_namespace
```

Two details already explain later behavior:

- **`pid_ns_for_children`.** A process's *own* PID namespace never changes
  (its PID is fixed at creation). What `unshare()` and `setns()` can change is
  the PID namespace that its **future children** will be created in. Section 3
  explains the consequences.
- **The user namespace is in `cred`, not `nsproxy`.** It is part of the
  process's credentials, because it determines what the process's UIDs and
  capabilities mean. Section 7 explains this.

## Namespaces as files: `/proc/<pid>/ns`

Each namespace instance is exposed as a special file:

```console
$ ls -l /proc/$$/ns
lrwxrwxrwx 1 alice alice 0 ... cgroup -> 'cgroup:[4026531835]'
lrwxrwxrwx 1 alice alice 0 ... ipc -> 'ipc:[4026531839]'
lrwxrwxrwx 1 alice alice 0 ... mnt -> 'mnt:[4026531841]'
lrwxrwxrwx 1 alice alice 0 ... net -> 'net:[4026531840]'
lrwxrwxrwx 1 alice alice 0 ... pid -> 'pid:[4026531836]'
lrwxrwxrwx 1 alice alice 0 ... pid_for_children -> 'pid:[4026531836]'
lrwxrwxrwx 1 alice alice 0 ... time -> 'time:[4026531834]'
lrwxrwxrwx 1 alice alice 0 ... time_for_children -> 'time:[4026531834]'
lrwxrwxrwx 1 alice alice 0 ... user -> 'user:[4026531837]'
lrwxrwxrwx 1 alice alice 0 ... uts -> 'uts:[4026531838]'
```

- The number in brackets is the inode number of the namespace on the internal
  `nsfs` filesystem. **Two processes are in the same namespace if and only if
  the links show the same number** (and device, which is the same for all
  namespace files).
- These are magic links (Chapter 01 §4). Opening one gives a **file descriptor
  that refers to the namespace**. That descriptor can be passed to `setns()`.

## The three system calls

```c
#define _GNU_SOURCE
#include <sched.h>

int clone(int (*fn)(void *), void *stack, int flags, void *arg, ...);
int unshare(int flags);
int setns(int fd, int nstype);
```

| Call | What it does | Analogy |
|---|---|---|
| `clone(CLONE_NEW*)` | creates a **child** in new namespaces | "start a new process in a new room" |
| `unshare(CLONE_NEW*)` | moves the **calling process** into new namespaces | "build a new room around me" |
| `setns(fd, type)` | moves the calling process into an **existing** namespace | "walk into an existing room" |

For `setns()`, `fd` is an open `/proc/<pid>/ns/<type>` file. Since Linux 5.8,
`fd` may also be a **pidfd** (a file descriptor for a process), and `nstype`
may combine several `CLONE_NEW*` flags to join several namespaces of that
process at once, atomically.

Command-line tools wrap these calls:

| Tool | Wraps | Example |
|---|---|---|
| `unshare` | `unshare()` then `execve()` (with `--fork`, a `fork()` in between) | `sudo unshare --uts bash` |
| `nsenter` | `setns()` then `execve()` | `sudo nsenter --target 4242 --uts --net bash` |
| `lsns` | reads `/proc/*/ns/*` | `lsns --type net` |
| `ip netns` | `unshare(CLONE_NEWNET)` + a bind mount (see below) | `ip netns add blue` |

## Privilege

Creating any namespace **other than a user namespace** requires the
`CAP_SYS_ADMIN` capability. In this chapter, read that as "requires root".
Creating a **user namespace** does not require privilege, and a process that
creates one gains capabilities *inside* it. Section 7 explains why that is
safe, and section 8 shows how an unprivileged user can therefore create the
other namespaces too.

## Lifetime: what keeps a namespace alive

A namespace exists as long as something references it:

1. **a process is a member** of it (or, for PID and time namespaces, still uses
   it for children);
2. **an open file descriptor** refers to its `/proc/<pid>/ns/*` file;
3. **a bind mount** of its `/proc/<pid>/ns/*` file exists somewhere;
4. for some types, a **dependent object** exists, such as a child PID namespace
   or a namespace owned by a user namespace.

When the last reference disappears, the kernel destroys the namespace and its
resources: a network namespace's virtual interfaces are deleted, an IPC
namespace's shared memory segments are freed, and a mount namespace's mounts are
unmounted.

Rule 3 is how `ip netns add blue` creates a network namespace that survives
without any process: it bind-mounts the namespace file to `/run/netns/blue`
(Chapter 02 §3 said files can be bind mounts; here the "file" is an `nsfs`
inode). Container runtimes use the same technique to keep a pod's network
namespace alive between container restarts.

## Hierarchy and ownership

Most namespaces are flat: a new UTS namespace has no relationship to the one it
was created from. Two types are **hierarchical**:

- **PID namespaces** form a tree. A process is visible, with a different PID,
  in its own PID namespace and in every ancestor (section 3).
- **User namespaces** form a tree. Additionally, **every namespace of any type
  is owned by a user namespace**: the user namespace of the process that created
  it. Privilege over a namespace's resources is checked against its owning user
  namespace (section 7).

The `ioctl()` operations `NS_GET_PARENT` and `NS_GET_USERNS` on a namespace file
descriptor (see `ioctl_ns(2)`) expose these relationships; `lsns -o+PNS,ONS`
(where supported) prints them.

## What namespaces do *not* do

- They do **not limit resource usage.** A process in its own namespaces can
  still use all CPU and memory. That is cgroups (Chapter 04).
- They do **not restrict privileged operations on non-namespaced resources.**
  Loading a kernel module, changing the system clock (`CLOCK_REALTIME`), or
  writing global sysctls affect the host, whatever namespaces a process is in.
  That is capabilities and seccomp (Chapters 05–06).
- They do **not give a separate kernel.** All namespaces share one kernel; a
  kernel bug is reachable from any of them.

## Why this matters for containers

A container is a process tree placed in a set of new namespaces, usually mount,
UTS, IPC, PID, network, and cgroup, and optionally user. "Entering a container"
(`docker exec`, `kubectl exec`) is `setns()` into those namespaces, followed by
`execve()`. A Kubernetes pod is, at the namespace level, several containers
that share network, IPC, and optionally PID namespaces, while having separate
mount namespaces.

## Evidence

Lab: [`lab-01-namespace-files-and-tools`](../../labs/03-namespaces/lab-01-namespace-files-and-tools/)

## Further Reading

- [`namespaces(7)`](https://man7.org/linux/man-pages/man7/namespaces.7.html)
  — the overview page: types, `/proc/<pid>/ns`, lifetime rules, and
  `/proc/sys/user` limits. Read all of it.
- [`unshare(2)`](https://man7.org/linux/man-pages/man2/unshare.2.html) and
  [`setns(2)`](https://man7.org/linux/man-pages/man2/setns.2.html) — exact
  semantics and restrictions per namespace type (for example, which ones
  require a single-threaded caller). Chapter 11 depends on these restrictions.
- [`ioctl_ns(2)`](https://man7.org/linux/man-pages/man2/ioctl_ns.2.html) —
  how to discover namespace parents and owners programmatically.
- [`lsns(8)`](https://man7.org/linux/man-pages/man8/lsns.8.html),
  [`unshare(1)`](https://man7.org/linux/man-pages/man1/unshare.1.html),
  [`nsenter(1)`](https://man7.org/linux/man-pages/man1/nsenter.1.html) — the
  tools used in every lab. `unshare(1)` documents important defaults, such as
  making mounts private.
- Michael Kerrisk, LWN, ["Namespaces in operation, part 1: namespaces overview"](https://lwn.net/Articles/531114/)
  (2013) — the start of the classic seven-part series by the man-pages
  maintainer. Some details are dated (for example, cgroup and time namespaces
  did not exist yet), but the explanations remain the best introduction.
- Kernel source: [`include/linux/nsproxy.h`](https://elixir.bootlin.com/linux/v6.12/source/include/linux/nsproxy.h)
  — `struct nsproxy`, about 20 lines, matching the diagram above; and
  [`kernel/nsproxy.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/nsproxy.c),
  `copy_namespaces()`, called from `copy_process()` (Chapter 01 §3).
