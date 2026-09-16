# References — Chapter 01: Linux Process Fundamentals

Sources are grouped by type and ordered by how useful they are for this
chapter. Every entry says **why** it is worth your time. Primary sources come
first; secondary sources are included only where they explain something
unusually well.

Version note: kernel source links point to Linux **v6.12** on Elixir
(bootlin). The functions named here (`kernel_clone`, `copy_process`,
`begin_new_exec`, `find_new_reaper`) exist under these names in kernels from
roughly 5.10 onward. `kernel_clone()` was called `_do_fork()` in older
kernels, so older articles may use that name.

## Linux man-pages (primary)

The [Linux man-pages project](https://man7.org/linux/man-pages/) documents the
kernel's user-space interface and glibc wrappers. It is maintained close to the
kernel and is the closest thing to a specification of syscall behavior.

| Page | Why read it | Used in |
|---|---|---|
| [`syscalls(2)`](https://man7.org/linux/man-pages/man2/syscalls.2.html) | Complete syscall list with introduction versions; calibrates how small the kernel API is. | §1 |
| [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html) | Per-architecture calling conventions: how user space actually enters the kernel. | §1, Lab 01 |
| [`vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html) | Explains calls that never reach the kernel and never appear in `strace`. | §1, Lab 01 |
| [`fork(2)`](https://man7.org/linux/man-pages/man2/fork.2.html) | The exact list of attributes a child does not inherit. | §3 |
| [`execve(2)`](https://man7.org/linux/man-pages/man2/execve.2.html) | The authoritative list of what `execve()` resets and preserves, plus setuid and interpreter-script rules. The basis for the inheritance tables. | §3, §6, §7 |
| [`clone(2)`](https://man7.org/linux/man-pages/man2/clone.2.html) | All `CLONE_*` flags, `clone()` vs `clone3()`. You will return to it for namespaces. | §3, Lab 04 |
| [`posix_spawn(3)`](https://man7.org/linux/man-pages/man3/posix_spawn.3.html) | How glibc (and therefore the JVM) starts processes efficiently. | §3 |
| [`wait(2)`](https://man7.org/linux/man-pages/man2/wait.2.html) | Zombies and reaping, precisely. | §2, Lab 02 |
| [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) | `PR_SET_CHILD_SUBREAPER` and `PR_SET_NO_NEW_PRIVS`; used by shims and runtimes. | §2, Lab 02 |
| [`proc(5)`](https://man7.org/linux/man-pages/man5/proc.5.html), [`proc_pid_status(5)`](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html) | Field-level dictionary of procfs. | §4, Lab 05 |
| [`open(2)`](https://man7.org/linux/man-pages/man2/open.2.html) | "Open file descriptions" and `O_CLOEXEC`; the three-layer fd model. | §5, Lab 06 |
| [`dup(2)`](https://man7.org/linux/man-pages/man2/dup.2.html), [`fcntl(2)`](https://man7.org/linux/man-pages/man2/fcntl.2.html) | Descriptor flags vs file status flags. | §5 |
| [`signal(7)`](https://man7.org/linux/man-pages/man7/signal.7.html) | Best overview of dispositions, masks, and pending signals. | §6, Lab 07 |
| [`sigaction(2)`](https://man7.org/linux/man-pages/man2/sigaction.2.html), [`kill(2)`](https://man7.org/linux/man-pages/man2/kill.2.html) | Installing handlers; permission rules; the PID 1 rule. | §2, §6 |
| [`credentials(7)`](https://man7.org/linux/man-pages/man7/credentials.7.html) | Process IDs, sessions, and the four UID types in one page. | §6, §7 |
| [`setresuid(2)`](https://man7.org/linux/man-pages/man2/setresuid.2.html), [`setgroups(2)`](https://man7.org/linux/man-pages/man2/setgroups.2.html) | Rules for changing credentials. | §7, Lab 08 |
| [`strace(1)`](https://man7.org/linux/man-pages/man1/strace.1.html), [`setpriv(1)`](https://man7.org/linux/man-pages/man1/setpriv.1.html) | The two tools that turn claims in this chapter into observations. | Labs |

## Linux kernel documentation and source (primary)

- [Kernel docs: The /proc Filesystem](https://docs.kernel.org/filesystems/proc.html)
  — maintained with the code; table 1-2 documents every line of
  `/proc/<pid>/status`.
- [Kernel docs: Credentials in Linux](https://docs.kernel.org/security/credentials.html)
  — how `struct cred` is referenced and replaced atomically. Background for
  reading runtime code that changes credentials.
- [`include/linux/sched.h`](https://elixir.bootlin.com/linux/v6.12/source/include/linux/sched.h)
  — `struct task_struct`. Locate `mm`, `files`, `fs`, `cred`, `nsproxy`,
  `cgroups`, `seccomp`; do not read the rest.
- [`kernel/fork.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/fork.c)
  — `kernel_clone()` and `copy_process()`. The sequence of `copy_*()` calls
  is the "share or copy" decision list from §3.
- [`fs/exec.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/exec.c)
  — `do_execveat_common()` and `begin_new_exec()`: where threads are killed,
  close-on-exec descriptors closed, and handlers reset.
- [`kernel/exit.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/exit.c)
  — `find_new_reaper()`: the reparenting rule in about 40 lines.
- [`fs/proc/base.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/proc/base.c)
  — `tgid_base_stuff[]`: the list of entries in `/proc/<pid>/`.

## Runtime and JVM documentation (primary, for connections)

- Oracle, [Java SE 21 Troubleshooting Guide: Handle Signals and Exceptions](https://docs.oracle.com/en/java/javase/21/troubleshoot/handle-signals-and-exceptions.html)
  — which signals HotSpot uses internally and which trigger shutdown hooks.
- [runc security advisory GHSA-xr7r-f8xq-vfvv (CVE-2024-21626)](https://github.com/opencontainers/runc/security/advisories/GHSA-xr7r-f8xq-vfvv)
  — a real container breakout caused by a leaked file descriptor. Short, and
  fully understandable after §5.
- [krallin/tini](https://github.com/krallin/tini) — README explains why
  containers need a real init process: reaping and signal forwarding (§2, §6).

## Secondary sources (selected)

- Michael Kerrisk, *The Linux Programming Interface* (No Starch Press, 2010),
  chapters 6 (processes), 9 (credentials), 20–22 (signals), 24–28 (process
  creation, termination, `execve`). Written by the long-time man-pages
  maintainer; the most complete and careful book on this material. Some
  details (for example `clone3()`, pidfds) postdate it, so cross-check with
  current man pages.
- Andrew Baumann, Jonathan Appavoo, Orran Krieger, Timothy Roscoe,
  ["A fork() in the road"](https://www.microsoft.com/en-us/research/publication/a-fork-in-the-road/)
  (HotOS 2019) — a critical perspective on `fork()`. Useful to understand
  why `posix_spawn` and `CLONE_VFORK` exist.
- Hao Chen, David Wagner, Drew Dean,
  ["Setuid Demystified"](https://www.usenix.org/legacy/publications/library/proceedings/sec02/full_papers/chen/chen.pdf)
  (USENIX Security 2002) — why the older setuid API family is confusing.
- Snyk, [Leaky Vessels: Docker and runc container breakout vulnerabilities](https://snyk.io/blog/leaky-vessels-docker-runc-container-breakout-vulnerabilities/)
  (2024) — a readable walkthrough of CVE-2024-21626.
