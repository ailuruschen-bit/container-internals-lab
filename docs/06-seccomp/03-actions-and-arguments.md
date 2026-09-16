# 3. Filter Actions and Argument Matching

## Filter actions

A seccomp filter returns a 32-bit value whose high bits select an **action** and
whose low 16 bits can carry data (an errno or a trap number). From least to most
severe:

| Action (libseccomp name) | Kernel constant | Effect |
|---|---|---|
| `SCMP_ACT_ALLOW` | `SECCOMP_RET_ALLOW` | run the syscall normally |
| `SCMP_ACT_LOG` | `SECCOMP_RET_LOG` | run it, but write an audit log record (needs kernel config/allow) |
| `SCMP_ACT_NOTIFY` | `SECCOMP_RET_USER_NOTIF` | suspend the call and hand it to a **user-space supervisor** (Linux 5.0+) |
| `SCMP_ACT_TRACE(n)` | `SECCOMP_RET_TRACE` | notify an attached `ptrace` tracer; without one, fail with `ENOSYS` |
| `SCMP_ACT_ERRNO(e)` | `SECCOMP_RET_ERRNO` | do **not** run the syscall; return `-e` to the caller |
| `SCMP_ACT_TRAP` | `SECCOMP_RET_TRAP` | do not run it; send `SIGSYS` to the thread (catchable) |
| `SCMP_ACT_KILL` / `SCMP_ACT_KILL_THREAD` | `SECCOMP_RET_KILL_THREAD` | kill the **calling thread** with `SIGSYS` |
| `SCMP_ACT_KILL_PROCESS` | `SECCOMP_RET_KILL_PROCESS` | kill the **whole thread group** (Linux 4.14+) |

When multiple stacked filters return different actions for the same syscall, the
kernel takes the **most severe** (highest-priority) one. So a later filter can
only make the policy stricter.

### ERRNO vs KILL: the container trade-off

Most container profiles use `SCMP_ACT_ERRNO` for blocked syscalls, usually with
`EPERM` (or `ENOSYS` for whole unimplemented groups). Reasons:

- **Compatibility.** A program that probes for an optional syscall (for example a
  newer `clone3`, or `statx`) and falls back on failure keeps working when the
  syscall returns an error, but crashes if the process is killed. glibc and Go
  both probe for newer syscalls this way.
- **Debuggability.** An `EPERM` in a log is easier to diagnose than a process
  that vanished.

Strict profiles, and seccomp used purely for isolation of untrusted code, prefer
`KILL_PROCESS`, because silently returning an error can let a determined attacker
enumerate the policy.

### SCMP_ACT_NOTIFY: syscalls handled in user space

`SECCOMP_RET_USER_NOTIF` is powerful enough to deserve mention. The syscall is
paused, and a **supervisor process** holding a notification file descriptor
receives the arguments, decides what to do (including performing the operation on
the caller's behalf), and returns a result. Container runtimes use it to safely
**emulate** privileged syscalls: for example, letting a container call `mount()`
for a specific, checked filesystem, or handling `mknod` for permitted devices,
without granting the capability. This is how some rootless and gVisor-adjacent
setups give controlled access to operations that would otherwise be blocked.

## Matching syscall arguments

A filter can inspect the six syscall arguments in `seccomp_data.args[]`, not just
the syscall number. This allows rules like "allow `ioctl` only with safe request
codes" or "block `clone` when it requests a new user namespace".

With libseccomp:

```c
/* Block clone() when the CLONE_NEWUSER flag (args[0] & CLONE_NEWUSER) is set */
seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 1,
                 SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER));

/* Allow setuid() only for uid == 1000 */
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setuid), 1,
                 SCMP_A0(SCMP_CMP_EQ, 1000));
```

### The critical limitation: no dereferencing

A seccomp filter can compare the **argument values** (the register contents), but
it **cannot follow pointers**. It cannot read the string a `open(path, ...)`
points to, or inspect a `struct` passed by address. Two reasons:

1. **Safety and speed.** cBPF has no safe way to read arbitrary user memory, which
   could fault or block.
2. **TOCTOU.** Time-of-check-to-time-of-use: even if the kernel copied the string
   to check it, another thread sharing the address space could change it between
   the check and the kernel's use of it. Filtering on pointed-to data would be
   racy and insecure.

Consequence: **seccomp filters on paths, filenames, or structure contents are
not possible.** You cannot write a seccomp rule "allow `open` only under `/tmp`".
Path-based decisions belong to other mechanisms (mount namespaces, LSMs like
AppArmor/SELinux, or `SCMP_ACT_NOTIFY` with a supervisor that safely reads the
memory). This is a frequent misconception, and it shapes what a seccomp profile
can and cannot enforce.

## Why this matters for containers

- The default container profile is an **allow list** with a default action of
  `ERRNO`, plus a few **argument-based** rules. The most important argument rule
  historically blocked `clone`/`unshare` with new-namespace flags and
  `personality` changes, to stop a container from creating nested user
  namespaces or switching execution domains. (This has been relaxed as user
  namespaces became safer; check the current profile.)
- Because seccomp cannot see paths, "which files a container may open" is decided
  by the mount namespace and root filesystem (Chapters 02, 07) and by LSMs, never
  by seccomp.
- `SCMP_ACT_NOTIFY` is how modern runtimes give a container controlled access to
  a few privileged syscalls without capabilities.

## Evidence

Lab: [`lab-03-seccomp-arguments`](../../labs/06-seccomp/lab-03-seccomp-arguments/)

## Further Reading

- Kernel docs: [Seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html),
  sections "Return values" (the action precedence) and "Userspace Notification"
  (`SECCOMP_RET_USER_NOTIF`). Definitive for this section.
- [`seccomp_unotify(2)`](https://man7.org/linux/man-pages/man2/seccomp_unotify.2.html)
  — the full user-notification protocol, including how a supervisor reads the
  caller's memory safely via `/proc/<pid>/mem` and the `SECCOMP_IOCTL_NOTIF_*`
  ioctls.
- libseccomp: [seccomp_rule_add(3)](https://man7.org/linux/man-pages/man3/seccomp_rule_add.3.html)
  — the `SCMP_CMP_*` argument comparators and their limits.
- [`seccomp(2)`](https://man7.org/linux/man-pages/man2/seccomp.2.html),
  "Return value" — the precedence order of actions in stacked filters.
- LWN, ["A seccomp overview"](https://lwn.net/Articles/656307/) (2015) — a clear
  narrative of the design, including why filters cannot dereference pointers.
