# Chapter 05 — Capabilities: Splitting Root

## Why this chapter exists

Chapters 03 and 04 left one question open. A default Docker or Kubernetes
container runs its process as **UID 0**, and does **not** use a user namespace.
To the kernel, that process is real root. Yet it cannot load kernel modules,
change the system clock, mount filesystems, or reconfigure the host's network.
What stops it?

The main answer is **capabilities**: Linux divides the privileges traditionally
associated with UID 0 into about 40 separate units, and a process can hold any
subset of them. Container runtimes start the application with a reduced set.
"Root inside a container" is therefore UID 0 **minus most capabilities**, plus
the namespace and cgroup restrictions you already know, plus seccomp and
security modules (Chapter 06).

Chapter 01 §7 introduced capabilities in one paragraph and Chapter 03 §7 showed
that capabilities are checked relative to user namespaces. This chapter gives
the complete rules.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [From root to capabilities](01-root-to-capabilities.md) | Why all-or-nothing root was split, and what the capabilities are. |
| 2 | [Capability sets and file capabilities](02-capability-sets.md) | Five per-thread sets and three per-file fields. |
| 3 | [Capabilities across execve](03-capabilities-across-execve.md) | The transformation rules, root special cases, ambient capabilities, securebits. |
| 4 | [Capabilities and user namespaces](04-capabilities-and-user-namespaces.md) | Namespace-relative checks, precisely. |
| 5 | [Root in a container](05-root-in-a-container.md) | The default capability set, what it allows, and where isolation fails. |
| 6 | [Chapter summary](06-summary.md) | What changed, what did not. |

Labs: [`labs/05-capabilities/`](../../labs/05-capabilities/).
References: [`references/05-capabilities.md`](../../references/05-capabilities.md).

## Prerequisites

- [Chapter 01](../01-linux-process/): `execve()` (§3), credentials, setuid (§7).
- [Chapter 02](../02-linux-filesystem/): `nosuid` (§2), device nodes (§5).
- [Chapter 03](../03-namespaces/): user namespaces (§7).

## Environment

Linux VM with `sudo`, `libcap2-bin` (`capsh`, `getcap`, `setcap`, `getpcaps`),
`util-linux` (`setpriv`), `python3`, `gcc`.
