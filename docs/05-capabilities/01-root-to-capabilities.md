# 1. From Root to Capabilities

## The problem: all-or-nothing privilege

Traditional Unix has exactly two kinds of processes for permission checks:

- **privileged**: effective UID 0, which bypasses essentially all kernel
  permission checks;
- **unprivileged**: everything else, subject to full checks based on UIDs,
  GIDs, and file modes.

Many programs need exactly one privileged operation:

| Program | The one privileged thing it needs |
|---|---|
| `ping` | open a raw network socket |
| a web server | bind to port 80 or 443 |
| `ntpd` / `chronyd` | set the system clock |
| `passwd` | write `/etc/shadow` |
| a backup agent | read every file regardless of permissions |

Under the traditional model, each of these must run as root or be setuid-root,
which grants them **every** privilege. A bug in the web server's HTTP parser
becomes a way to load kernel modules. This violates the **principle of least
privilege**: a component should have only the privileges it needs.

## The Linux abstraction: capabilities

Starting with Linux 2.2, the kernel divides root's privileges into distinct
units called **capabilities**. Each privileged kernel code path checks for one
specific capability instead of checking `euid == 0`.

```c
/* before (conceptually)          after (actual kernel pattern) */
if (current_euid() != 0)          if (!capable(CAP_SYS_TIME))
        return -EPERM;                    return -EPERM;
```

(The kernel's traditional root check was always expressed through a
function, `suser()`, and was gradually replaced by `capable()` calls; the
snippet illustrates the idea.)

`capable(CAP_X)` asks: "does the current task have `CAP_X` in its **effective**
set, in the initial user namespace?" The namespace-relative version,
`ns_capable(ns, CAP_X)`, asks the same question relative to a user namespace
(section 4).

## The capabilities

Linux 6.x defines 41 capabilities (numbers 0–40). They are not equally
important. A useful grouping:

| Group | Capabilities (without the `CAP_` prefix) | What they bypass or allow |
|---|---|---|
| **File permission overrides** | `DAC_OVERRIDE`, `DAC_READ_SEARCH`, `FOWNER`, `FSETID`, `CHOWN`, `LINUX_IMMUTABLE` | ignore read/write/execute bits; read any directory; act as file owner; keep setuid bits on modify; change file ownership; modify immutable files |
| **Identity** | `SETUID`, `SETGID`, `SETPCAP`, `SETFCAP` | change UIDs/GIDs arbitrarily; manipulate capability sets and file capabilities |
| **Processes** | `KILL`, `SYS_PTRACE`, `SYS_NICE`, `SYS_RESOURCE` | signal any process; trace/inspect any process; raise priorities; exceed resource limits |
| **Networking** | `NET_BIND_SERVICE`, `NET_RAW`, `NET_ADMIN`, `NET_BROADCAST` | bind ports < 1024; raw/packet sockets; configure interfaces, routes, firewall |
| **Devices and hardware** | `MKNOD`, `SYS_RAWIO`, `SYS_TTY_CONFIG`, `WAKE_ALARM`, `BLOCK_SUSPEND` | create device nodes; raw I/O ports and memory; configure terminals |
| **System** | `SYS_TIME`, `SYS_BOOT`, `SYS_MODULE`, `SYSLOG`, `SYS_PACCT`, `SYS_CHROOT`, `IPC_LOCK`, `IPC_OWNER`, `LEASE` | set clocks; reboot; load kernel modules; read kernel log; `chroot()`; lock memory |
| **Security subsystems** | `AUDIT_WRITE`, `AUDIT_CONTROL`, `AUDIT_READ`, `MAC_ADMIN`, `MAC_OVERRIDE` | audit log; configure/bypass LSMs such as AppArmor and SELinux |
| **Split from SYS_ADMIN** (5.8+) | `BPF`, `PERFMON`, `CHECKPOINT_RESTORE` | load BPF programs; performance monitoring; checkpoint/restore operations |
| **Everything else** | `SYS_ADMIN` | `mount()`, `unshare()`/`setns()` for most namespaces, `sethostname()`, `pivot_root()`, many device `ioctl`s, and hundreds of other checks |

### `CAP_SYS_ADMIN`: "the new root"

When kernel developers added a privileged operation and could not decide which
capability should protect it, they often chose `CAP_SYS_ADMIN`. It now guards
so many unrelated operations that holding it is close to holding full root. The
`capabilities(7)` man page itself warns against using it for new features. For
containers, the most important consequence is simple: **a container with
`CAP_SYS_ADMIN` (in the initial user namespace) is not meaningfully confined**.

## Why capabilities are not enough on their own

Capabilities restrict *privileged operations*. They say nothing about:

- ordinary operations every process may perform, such as opening a file that
  happens to be world-writable, or calling rarely used system calls with large
  attack surfaces (Chapter 06, seccomp);
- **file ownership**: a process with UID 0 and **no capabilities at all** still
  owns every file owned by UID 0, and can read and write them through normal
  owner permission bits (section 5, and Lab 04).

## Why this matters for containers

- Container runtimes start applications with a **small default set** of
  capabilities (14 in Docker and containerd), and let operators add or drop
  capabilities (`--cap-add`, `--cap-drop`, Kubernetes
  `securityContext.capabilities`).
- `--privileged` grants **all** capabilities (and removes several other
  protections). Understanding the table above is what lets you say which
  specific capability an application actually needs instead.

## Evidence

Lab: [`lab-01-capability-sets`](../../labs/05-capabilities/lab-01-capability-sets/)

## Further Reading

- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html)
  — the definitive list of capabilities and what each one allows, plus the
  "Notes to kernel developers" warning about `CAP_SYS_ADMIN`. This chapter is
  largely a guided reading of this page.
- Michael Kerrisk, LWN, ["CAP_SYS_ADMIN: the new root"](https://lwn.net/Articles/486306/)
  (2012) — why one capability absorbed so many privileges.
- Kernel source: [`include/uapi/linux/capability.h`](https://elixir.bootlin.com/linux/v6.12/source/include/uapi/linux/capability.h)
  — every capability number with a comment listing what it controls. A faster
  way than grepping the kernel to see what a capability covers.
- Kernel source: [`kernel/capability.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/capability.c)
  — `capable()`, `ns_capable()`, and `has_ns_capability()`.
