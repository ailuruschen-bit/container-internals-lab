# 9. Chapter Summary: Views, Not Walls

## The model you should now have

A namespace gives a group of processes their own instance of one global kernel
resource. Membership is a property of each process (`nsproxy`, and `cred` for
the user namespace), inherited across `fork()` and preserved across `execve()`.
The kernel consults it when answering system calls.

| Namespace | Isolates | Created by | Key surprise |
|---|---|---|---|
| UTS | hostname, domain name | `CLONE_NEWUTS` | kernel release is not isolated |
| PID | PID numbering | `CLONE_NEWPID` | `unshare`/`setns` affect only children; PID 1 semantics; `/proc` must be remounted |
| Mount | mount tree | `CLONE_NEWNS` | files are not isolated; propagation is inherited (shared on systemd) |
| IPC | System V IPC, POSIX mqueues, IPC sysctls | `CLONE_NEWIPC` | `/dev/shm` is a mount, not an IPC object |
| Network | devices, addresses, routes, firewall, ports | `CLONE_NEWNET` | starts with only `lo`, down; connecting is someone else's job |
| User | UID/GID mapping, capability scope, ownership | `CLONE_NEWUSER` | root inside has capabilities only over namespaces it owns |
| Cgroup | view of the cgroup hierarchy | `CLONE_NEWCGROUP` | Chapter 04 |
| Time | monotonic/boot clock offsets | `CLONE_NEWTIME` | rarely used by containers |

The three system calls:

```text
clone(CLONE_NEW*)   new process, new namespaces
unshare(CLONE_NEW*) this process, new namespaces   (PID: children only)
setns(fd, type)     this process, existing namespace (PID: children only)
```

## Updated properties table

Rows added to the table from Chapter 01 §8:

| Property | After `fork()` | After `execve()` | Change with |
|---|---|---|---|
| UTS, IPC, network, cgroup namespace | same as parent unless `CLONE_NEW*` | preserved | `unshare`, `setns` |
| Mount namespace | same unless `CLONE_NEWNS` | preserved | `unshare`, `setns` (single-threaded, not sharing `CLONE_FS`) |
| PID namespace (own) | same unless `CLONE_NEWPID` | preserved | **never** |
| PID namespace for children | inherited | preserved | `unshare`, `setns` |
| User namespace | same unless `CLONE_NEWUSER` | preserved; capabilities recalculated | `unshare`, `setns` (single-threaded) |

## The sentence to remember

> A namespace changes **what a process can see and name**, not **how much it
> can use** or **what it is allowed to do** to the shared kernel.

## What has *not* been explained yet

- **Resource limits**: nothing in this chapter stops a process in six
  namespaces from using all CPUs and memory. Chapter 04.
- **Privilege without a user namespace**: default containers run as real UID 0.
  Chapter 05 explains how capabilities limit that.
- **Syscall surface**: every syscall is still available. Chapter 06.
- **A separate root filesystem**: a new mount namespace is a *copy* of the
  host's tree. Chapters 07–08.

## Self-check questions

1. What exactly does `readlink /proc/<pid>/ns/net` return, and how do you use it
   to decide whether two processes share a network namespace? (§1)
2. List four things that keep a namespace alive. Which one does `ip netns add`
   use? (§1)
3. Why does `uname -r` inside a container show the host's kernel? (§2)
4. Why does `sudo unshare --pid bash` fail after the first command, and what
   does `--fork` change? (§3)
5. A container's PID 1 is `sh -c "java -jar app.jar"`. Describe exactly what the
   kernel does with `SIGTERM` from the host, then `SIGKILL`. (§3)
6. On a systemd host, why can a mount made in a new mount namespace appear on
   the host? What does runc do to prevent it? (§4)
7. Two containers share an IPC namespace but not a mount namespace. Can they
   share memory through `shm_open()`? (§5)
8. Draw the path of a packet from a process in a namespace on a bridge to an
   Internet server and back, naming every NAT step. (§6)
9. Why can an unprivileged user set a hostname inside `unshare -Ur -u`, but not
   with `unshare -Ur` alone? (§7)
10. Why must ID maps be written by the parent, and why must the child wait? (§7, §8)

## Next: Chapter 04 — cgroups v2

Namespaces isolate views; they do not limit consumption. Chapter 04 explains
control groups v2: the hierarchy, controllers for CPU, memory, PIDs, and I/O,
the `/sys/fs/cgroup` interface, moving processes into groups, and how limits
and accounting work, ending with the cgroup namespace and how the JVM reads
container limits.
