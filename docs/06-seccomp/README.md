# Chapter 06 — seccomp and no_new_privs

## Why this chapter exists

Chapter 05 ended with a gap. Capabilities gate *privileged* operations, but a
container can still **call every system call in the kernel**, including large,
rarely audited ones. The Linux kernel exposes roughly 400 system calls; a
typical application uses a few dozen. Each unused syscall is attack surface: a
bug in an obscure syscall's implementation is reachable from any process that
can call it, capabilities or not.

**seccomp** (secure computing) lets a process **restrict which system calls it,
and its descendants, may make**. Container runtimes install a seccomp filter
that allows the common syscalls and blocks or restricts the dangerous or
unnecessary ones. This is the fourth confinement layer from Chapter 05 §5, and
the last kernel primitive before we assemble a container by hand (Chapter 09).

This chapter also finally defines **`no_new_privs`**, which appeared throughout
Chapters 02, 05, and here, and which seccomp usually requires.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [no_new_privs](01-no-new-privs.md) | A one-way flag that forbids gaining privileges at `execve()`. |
| 2 | [seccomp modes and BPF filters](02-seccomp-modes-and-filters.md) | Strict mode, filter mode, how a classic BPF program decides each syscall. |
| 3 | [Filter actions and argument matching](03-actions-and-arguments.md) | ERRNO, KILL, TRAP, LOG, NOTIF; matching syscall arguments and their limits. |
| 4 | [The container seccomp profile](04-container-profile.md) | The default Docker/Kubernetes profile; a note on LSMs. |
| 5 | [Chapter summary](05-summary.md) | What changed, what did not. |

Labs: [`labs/06-seccomp/`](../../labs/06-seccomp/).
References: [`references/06-seccomp.md`](../../references/06-seccomp.md).

## Prerequisites

- [Chapter 01](../01-linux-process/): system calls (§1), `execve()` (§3), signals
  (§6).
- [Chapter 05](../05-capabilities/): capabilities and, in particular, the
  repeated mentions of `no_new_privs`.

## Environment

Linux VM with `sudo`, `gcc`, `python3`, `libseccomp` tools
(`seccomp` headers via `libseccomp-dev`, and `scmp_sys_resolver`),
`util-linux` (`setpriv`), and `strace`. Docker is optional for the last lab.
