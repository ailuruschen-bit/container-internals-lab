# 5. Chapter Summary: Shrinking the Syscall Surface

## The model you should now have

`no_new_privs` is a one-way, inherited flag that stops `execve()` from ever
granting new privileges (setuid, file capabilities). It is the key that lets an
unprivileged process install a **seccomp** filter.

seccomp restricts which **system calls** a process and its descendants may make.
Filter mode runs a **classic BPF** program on every syscall, given the syscall
number, architecture, and the six register arguments, and the program returns an
**action**: allow, log, notify user space, return an errno, trap, or kill.
Filters stack (most severe wins), are inherited across `fork()`, and are
preserved across `execve()`.

```text
                       every system call
                             │
        seccomp filter (cBPF on seccomp_data) ──► action
        ├─ ALLOW / LOG        → run it
        ├─ ERRNO(e)           → skip it, return -e            (container default)
        ├─ NOTIFY             → hand to a user-space supervisor
        ├─ TRAP               → SIGSYS (catchable)
        └─ KILL_THREAD/PROCESS→ SIGSYS, terminate
                             │ (if allowed)
                   capability checks inside the syscall       (Chapter 05)
```

Key limits: a filter **cannot dereference pointers**, so it cannot decide on
paths or structure contents; and it must **check the architecture first** or be
bypassable through alternate syscall ABIs.

## Updated properties table

| Property | After `fork()` | After `execve()` |
|---|---|---|
| `no_new_privs` | copied | preserved (never cleared) |
| seccomp mode and filters | copied | preserved |

## The sentence to remember

> Capabilities decide which privileged operations succeed; seccomp decides which
> system calls can be attempted at all. Together with namespaces and cgroups,
> and usually an LSM, they are the layers that confine a container, over one
> shared kernel.

## The five layers, assembled

This completes the confinement picture the repository has been building:

| Layer | Chapter | Restricts | A container without it |
|---|---|---|---|
| namespaces | 03 | what a process can **see and name** | sees host PIDs, mounts, network, users |
| cgroups | 04 | how much it can **consume** | can exhaust CPU, memory, PIDs, I/O |
| capabilities | 05 | which **privileged operations** succeed | root can act on the host |
| seccomp | 06 | which **syscalls** can be made | full kernel attack surface reachable |
| LSM (AppArmor/SELinux) | 06 (noted) | **mandatory** path/label access control | no path-based MAC |

All five share one kernel; a kernel vulnerability reachable through an allowed
syscall can defeat them, which is why reducing the syscall surface (seccomp) and
making the attacker unprivileged (user namespaces) matter so much.

## Self-check questions

1. Why does installing a seccomp filter without `CAP_SYS_ADMIN` require
   `no_new_privs`? (§1, §2)
2. What are the inputs a seccomp filter receives, and why must it check the
   architecture before the syscall number? (§2)
3. Why do container profiles usually return `ERRNO` instead of killing the
   process, and when is killing preferable? (§3)
4. Explain why you cannot write a seccomp rule "allow `open` only under `/tmp`",
   and name two mechanisms that can enforce that. (§3, and Chapters 02/07)
5. A syscall fails with `EPERM` inside a container but with `EINVAL` when run
   `--security-opt seccomp=unconfined`. What does that tell you? (§4)
6. Give two independent ways the default container configuration can block
   `mount()`. (§4, Chapter 05)
7. What can an LSM enforce that seccomp cannot, and vice versa? (§4, §3)

## Next: Chapter 07 — rootfs, chroot, and pivot_root

With process, filesystem, namespace, cgroup, capability, and seccomp primitives
all covered, the remaining pieces are the container's **root filesystem** and how
a process is placed inside it. Chapter 07 covers `chroot()` (and why it is not a
security boundary), `pivot_root()` inside a mount namespace, and mounting
`/proc`, `/dev`, and `/sys` for a new root, the last building block before we
assemble a container by hand.
