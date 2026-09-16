# 2. The Process Tree: PID, PPID, and PID 1

## The problem: who is responsible for a process?

When a process ends, something must happen to its exit status. A shell wants
to know if `make` succeeded. A supervisor wants to know if a service crashed.
The kernel therefore needs a rule for **who receives the result** of every
process. Linux's answer is a strict family tree.

## PIDs

Every process has a **process ID (PID)**, a positive integer assigned by the
kernel when the process is created. PIDs are allocated incrementally and wrap
around after reaching a maximum (see `/proc/sys/kernel/pid_max`, commonly
4194304 on 64-bit systems). A PID is unique **among the processes that
currently exist**, but it will eventually be reused after a process is gone.

Two consequences are worth remembering:

- A PID is not a durable identity. Code that stores a PID and signals it much
  later can hit an unrelated process. (Linux added `pidfd`, a file descriptor
  referring to a specific process, largely to solve this race.)
- A PID is a number **within a particular numbering space**. So far we have
  assumed there is one global space. Chapter 03 shows that the kernel can
  maintain several, which is why the same process can be PID 1 inside a
  container and PID 48213 on the host.

## PPID and the tree

Every process except the very first one is created by another process, its
**parent**. The parent's PID is the child's **parent process ID (PPID)**.

```console
$ ps -o pid,ppid,comm -H
    PID    PPID COMMAND
   2201    2200 bash
   2344    2201   ps
```

The ancestry of every process leads back to **PID 1**:

```text
PID 1  systemd (or another init)
 ├── sshd
 │    └── sshd (session)
 │         └── bash
 │              └── java -jar app.jar
 │                   ├── thread (TID only, not a separate process)
 │                   └── ...
 ├── containerd
 └── ...
```

Two special tasks sit outside this normal tree:

- **PID 0** is not a real process. It is the idle task of the first CPU,
  created by the kernel during boot. It appears as the PPID of PID 1 and PID 2.
- **PID 2 (`kthreadd`)** is the parent of kernel threads, such as
  `[kworker/0:1]`. Kernel threads have no user-space memory and show up in
  square brackets in `ps`.

## Exit status, `wait()`, and zombies

When a process exits (by returning from `main`, calling `exit()`, or being
killed by a signal), the kernel:

1. releases most of its resources: memory, open files, and so on;
2. keeps a small remnant of the `task_struct` containing the **exit status**
   and resource usage;
3. sends the parent a `SIGCHLD` signal.

The remnant stays until the parent collects it with one of the `wait` family
of syscalls: `wait4()` or `waitid()` (libc `wait()` and `waitpid()` are
wrappers). Collecting the status is called **reaping**.

A process that has exited but has not yet been reaped is a **zombie**. `ps`
shows it with state `Z` and `<defunct>`. A zombie uses no memory or CPU, but it
still holds its PID. If a parent never reaps its children, zombies accumulate,
and eventually the PID space (or a cgroup `pids` limit, Chapter 04) can be
exhausted, so no new processes can be created.

## Orphans and reparenting

What if the parent exits *before* the child? The child becomes an
**orphan**. The kernel does not leave it without a parent. It **reparents**
the orphan to:

1. the nearest ancestor that has marked itself as a **child subreaper**
   (with `prctl(PR_SET_CHILD_SUBREAPER, 1)`), if there is one; otherwise
2. **PID 1**.

The new parent is now responsible for reaping the orphan when it exits.

This is why PID 1 has an important duty: it must call `wait` for children it
never created. Traditional init systems (`systemd`, `sysvinit`) do this in a
loop. A normal application usually does not, because it only expects to reap
its own children.

`systemd --user` and several service managers use `PR_SET_CHILD_SUBREAPER` so
that the processes of a service are reparented to the service manager instead
of escaping to PID 1. As you will see later, container shims use the same
mechanism.

## Why PID 1 is special

PID 1 differs from other processes in three ways:

1. **Reaping duty.** As described above, orphans end up there.
2. **Signal protection.** The kernel does not deliver a signal to PID 1 if
   PID 1 has not installed a handler for it, even if the signal's default
   action would terminate a normal process. This protects the system from an
   accidental `kill -TERM 1`. (`SIGKILL` and `SIGSTOP` are also ignored for the
   host's PID 1.) Signals are covered in [section 6](06-signals.md).
3. **Its death is fatal.** If the host's PID 1 exits, the kernel panics
   ("Attempted to kill init!").

Hold on to these three properties. In a container, your application is often
PID 1 of its own PID numbering space (Chapter 03), and all three properties
apply to it. That explains two classic production problems:

- a Java application running as PID 1 that spawns shell scripts can
  accumulate zombies, because the JVM does not reap orphans it never created;
- an application running as PID 1 that has no `SIGTERM` handler does not stop
  on `SIGTERM`, so the stop request times out and the process is eventually
  killed with `SIGKILL`.

(The JVM does install a `SIGTERM` handler that runs shutdown hooks, so the
second problem more commonly affects shell scripts and small native programs
than Java itself.)

## Why this matters for containers

- A container's "main process" is simply the process the runtime `execve()`s
  at the end of setup. It has a parent on the host, just like any process.
- Something on the host must reap the container's main process and report its
  exit code. In the containerd architecture this is the **shim** process, which
  acts as a subreaper. You now know the mechanism it uses.
- The PID 1 behaviors above are the reason tools such as `tini` and
  `docker run --init` exist: a tiny init that installs signal handlers,
  forwards signals to the application, and reaps zombies.

## Evidence

Lab: [`lab-02-process-tree-and-reaping`](../../labs/01-linux-process/lab-02-process-tree-and-reaping/)

## Further Reading

- [`wait(2)`](https://man7.org/linux/man-pages/man2/wait.2.html) — read the
  NOTES section on zombies and on what happens when `SIGCHLD` is ignored. It is
  the authoritative description of reaping.
- [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html), entry
  `PR_SET_CHILD_SUBREAPER` — two paragraphs that explain the exact reparenting
  rule used by service managers and container shims.
- [`kill(2)`](https://man7.org/linux/man-pages/man2/kill.2.html), NOTES — the
  precise rule for which signals can be sent to PID 1.
- [`proc_pid_status(5)`](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
  — definitions of `State`, `PPid`, `Tgid`, and friends as you will read them
  in `/proc`.
- Kernel source: [`kernel/exit.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/exit.c),
  functions `find_new_reaper()` and `forget_original_parent()`. Short, readable,
  and a good first taste of kernel code: it is the reparenting rule written in C.
- [krallin/tini README](https://github.com/krallin/tini) — a concise explanation
  of why a container needs a real init, written by the author of the most
  widely used one.
