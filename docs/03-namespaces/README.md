# Chapter 03 — Namespaces

## Why this chapter exists

Chapter 01 established that the kernel answers every system call using the
calling process's properties. Chapter 02 showed that one of those properties,
the mount tree, decides what a path means. This chapter generalizes that idea.

Many kernel resources are, by default, **global**: one hostname, one list of
process IDs, one set of network interfaces, one mount tree, one mapping of user
IDs. A **namespace** wraps one kind of global resource so that a group of
processes gets its **own instance** of it. Processes in different namespaces
make the same system calls and receive different answers.

Namespaces are the reason a containerized process believes it is alone on the
machine. They are also purely a Linux kernel feature: everything in this
chapter works without any container software installed.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [What a namespace is](01-namespace-concepts.md) | Per-process views of global resources; `clone`, `unshare`, `setns`; lifetime. |
| 2 | [UTS namespace](02-uts-namespace.md) | The simplest namespace: hostname. The whole API in miniature. |
| 3 | [PID namespace](03-pid-namespace.md) | Nested PID numbering; PID 1 of a namespace; why `/proc` must be remounted. |
| 4 | [Mount namespace](04-mount-namespace.md) | A private copy of the mount tree; propagation across namespaces. |
| 5 | [IPC namespace](05-ipc-namespace.md) | System V IPC objects and POSIX message queues. |
| 6 | [Network namespace](06-network-namespace.md) | A whole network stack per namespace; veth pairs, bridges, NAT. |
| 7 | [User namespace](07-user-namespace.md) | UID/GID mappings and namespace-relative privilege. |
| 8 | [Combining namespaces](08-combining-namespaces.md) | Ordering, ownership, joining with `setns`, and a Go example. |
| 9 | [Chapter summary](09-summary.md) | What changed, what did not. |

Labs: [`labs/03-namespaces/`](../../labs/03-namespaces/).
References: [`references/03-namespaces.md`](../../references/03-namespaces.md).

### A note on the cgroup and time namespaces

Linux has eight namespace types. Six are covered here in depth. The **cgroup
namespace** virtualizes the view of the cgroup hierarchy, which cannot be
explained before cgroups themselves, so it is taught at the end of
[Chapter 04](../04-cgroups/). The **time namespace** (Linux 5.6) offsets the
monotonic and boot-time clocks, mainly for checkpoint/restore; it is described
briefly in section 1 and is not used by default container setups.

## Prerequisites

- [Chapter 01](../01-linux-process/): `clone()` flags (§3), `/proc` (§4), file
  descriptors (§5), signals and PID 1 (§2, §6), credentials (§7).
- [Chapter 02](../02-linux-filesystem/): mounts, `mountinfo`, bind mounts,
  mount propagation, procfs.

## Environment

Linux 5.10 or newer is recommended. Most labs need `sudo`. The user namespace
lab also works without root, but some distributions restrict unprivileged user
namespaces; the lab explains how to check. Tools: `util-linux` (`unshare`,
`nsenter`, `lsns`), `iproute2` (`ip`), `procps`, `gcc`, and `go` for the final
example.
