# 6. Signals

## The problem: interrupting a running process

A process usually runs its own code and asks the kernel for services through
system calls. But sometimes something *outside* the process needs its
attention right now:

- the user pressed Ctrl-C;
- an operator or supervisor wants the process to shut down;
- a child process exited;
- the process accessed invalid memory;
- it wrote to a pipe whose reader has gone away.

Linux delivers these events as **signals**: small, numbered, asynchronous
notifications sent to a process (or a specific thread) by the kernel or by
another process.

## Signal basics

A signal is identified by a number and a name. The ones that matter most for
this repository:

| Signal | No. (x86-64/arm64) | Default action | Typical source |
|---|---|---|---|
| `SIGHUP` | 1 | terminate | terminal closed; often reused as "reload config" |
| `SIGINT` | 2 | terminate | Ctrl-C in a terminal |
| `SIGQUIT` | 3 | terminate + core dump | Ctrl-\\; the JVM prints a thread dump instead |
| `SIGKILL` | 9 | terminate | `kill -9`; the kernel OOM killer. **Cannot be caught, blocked, or ignored** |
| `SIGSEGV` | 11 | terminate + core dump | invalid memory access (sent by the kernel) |
| `SIGPIPE` | 13 | terminate | writing to a pipe or socket with no reader |
| `SIGTERM` | 15 | terminate | polite shutdown request; default of `kill` |
| `SIGCHLD` | 17 | ignore | a child exited or stopped |
| `SIGSTOP` | 19 | stop | **cannot be caught, blocked, or ignored** |
| `SIGCONT` | 18 | continue | resume a stopped process |

The full list and architecture-specific numbers are in `signal(7)`.

A process is sent a signal with the `kill()` system call:

```c
#include <signal.h>
int kill(pid_t pid, int sig);
```

Despite the name, `kill()` sends any signal. Permission is required: the
sender's real or effective UID must match the target's real or saved UID, or
the sender must have the `CAP_KILL` capability (root normally does).

## What a process can do with a signal: dispositions

For each signal, a process has a **disposition**, stored in its signal handler
table (`sighand` in `task_struct`):

| Disposition | Set by | Effect |
|---|---|---|
| Default | initial state, `SIG_DFL` | the default action from the table above |
| Ignore | `SIG_IGN` | the signal is discarded |
| Handle | `sigaction()` with a function | the kernel interrupts the process and runs the function, then resumes |

Additionally, each thread has a **signal mask**: signals that are temporarily
*blocked*. A blocked signal is not lost; it stays **pending** until it is
unblocked. You can see all of these per process in `/proc/<pid>/status`:
`SigPnd`/`ShdPnd` (pending), `SigBlk` (blocked), `SigIgn` (ignored), and
`SigCgt` (caught, i.e. has a handler). Each is a hexadecimal bit mask where
bit *n−1* represents signal *n*.

`SIGKILL` and `SIGSTOP` are the exceptions: their disposition cannot be
changed, and they cannot be blocked. They are the kernel's guaranteed way to
stop a process.

## Signals across fork() and execve()

This follows from sections 3 and 5, but it is so important for containers that
it deserves its own table:

| Property | After `fork()` | After `execve()` |
|---|---|---|
| Handled signals (with a function) | copied | **reset to default** (the function no longer exists) |
| Ignored signals | copied | **stay ignored** |
| Signal mask | copied | preserved |
| Pending signals | cleared in the child | preserved |

The "stay ignored" rule has a real-world effect: if a parent process ignores
`SIGTERM` or `SIGPIPE` and then starts your application, your application
starts with that signal ignored, unless it explicitly resets it. Good process
supervisors and container runtimes reset signal dispositions and the signal
mask before `execve()` for this reason.

## Process groups, sessions, and the terminal

When you press Ctrl-C, which process receives `SIGINT`? Not just one: the
terminal driver sends it to every process in the **foreground process group**.

- A **process group** is a set of processes, typically one shell pipeline such
  as `cat log | grep ERROR | wc -l`. Its ID (PGID) is the PID of its leader.
  `kill(-pgid, sig)` sends a signal to the whole group.
- A **session** is a set of process groups, typically everything started from
  one login terminal. A session may have one **controlling terminal**.
- The shell moves jobs between foreground and background process groups
  (`fg`, `bg`, `&`).

You do not need job control details for containers. The part that matters is:
signals from a terminal go to a **process group**, while `kill <pid>` goes to
**one process**. A process that receives `SIGTERM` does not automatically pass
it to its children.

## Graceful shutdown: SIGTERM, then SIGKILL

Almost every process manager stops a process in two steps:

1. send `SIGTERM`, giving the application a chance to finish in-flight work,
   flush data, and exit;
2. if the process is still alive after a grace period, send `SIGKILL`.

If the process is killed by a signal, its parent's `wait()` reports "terminated
by signal N". Shells and many tools convert this to exit code **128 + N**, which
is why you see:

- **143** = 128 + 15 → stopped by `SIGTERM`;
- **137** = 128 + 9 → killed by `SIGKILL`, very often by the OOM killer.

### How the JVM handles signals

- `SIGTERM`, `SIGINT`, and `SIGHUP` start the JVM shutdown sequence, which runs
  registered **shutdown hooks** (`Runtime.addShutdownHook`). Frameworks such as
  Spring use these hooks for graceful shutdown. The exit code is 143 for
  `SIGTERM`.
- `SIGKILL` gives the JVM no chance to run any code. Shutdown hooks do not run.
- `SIGQUIT` makes the JVM print a thread dump to stdout and keep running.
- The JVM uses some signals internally (for example `SIGSEGV` for implicit null
  checks and safepoints), which is why a JVM has many bits set in `SigCgt`.

## PID 1 and signals, revisited

[Section 2](02-process-tree.md) stated that the kernel does not deliver signals
to PID 1 unless PID 1 has a handler for them. Now the rule is more precise: for
PID 1, a signal whose disposition is **default** is dropped rather than
performing its default action. A handled signal is delivered normally.

Combine this with two facts from this section:

- signals are not forwarded to children automatically;
- ignored dispositions are inherited across `execve()`;

and you have the complete explanation of the classic container stop problem.
If the container's PID 1 is a shell script (`sh -c "java -jar app.jar"`):

1. The runtime sends `SIGTERM` to PID 1, the shell.
2. The shell is PID 1 and has no `SIGTERM` handler, so the signal is dropped.
   (Even if it were delivered, the shell would not forward it to `java`.)
3. Java never sees `SIGTERM`; shutdown hooks never run.
4. After the grace period, the runtime sends `SIGKILL`. `SIGKILL` sent from
   *outside* the container cannot be dropped, so the shell dies, and the kernel
   then kills the remaining processes of the container (Chapter 03 explains
   why). Exit code 137.

The fix is to make the application itself PID 1 (`exec java ...`), or to use a
small init that handles signals and forwards them.

## Why this matters for containers

- "Stopping a container" is signal delivery: `SIGTERM` to the main process,
  a grace period, then `SIGKILL`. The mechanism is exactly the one above.
- Exit codes 137 and 143 in container status are the 128+N convention.
- Chapter 03 will let you *reproduce* the PID 1 signal behavior inside a PID
  namespace, without Docker.

## Evidence

Lab: [`lab-07-signals`](../../labs/01-linux-process/lab-07-signals/)

## Further Reading

- [`signal(7)`](https://man7.org/linux/man-pages/man7/signal.7.html) — the
  best single overview: dispositions, masks, pending signals, the standard
  signal table, and the behavior of syscalls interrupted by handlers.
- [`sigaction(2)`](https://man7.org/linux/man-pages/man2/sigaction.2.html) —
  how handlers are installed, including `SA_RESTART`, which explains why some
  syscalls fail with `EINTR`.
- [`kill(2)`](https://man7.org/linux/man-pages/man2/kill.2.html) — the
  permission rules and the special meanings of `pid` 0, -1, and negative
  values.
- [`credentials(7)`](https://man7.org/linux/man-pages/man7/credentials.7.html),
  section on process groups and sessions — a compact explanation of PGID, SID,
  and controlling terminals.
- Oracle, [Java SE troubleshooting guide: Handle signals and exceptions](https://docs.oracle.com/en/java/javase/21/troubleshoot/handle-signals-and-exceptions.html)
  — authoritative list of signals the HotSpot JVM uses and what `-Xrs` changes.
