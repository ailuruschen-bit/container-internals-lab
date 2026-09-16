# Lab 02 — A seccomp Filter: Allowlist, EPERM, and SIGSYS

## Goal

Produce evidence that:

1. an unprivileged process can install a seccomp filter after `no_new_privs`;
2. a blocked syscall can be made to return `EPERM` or to kill the process;
3. the filter is applied to the program executed **after** it is installed;
4. `strace` shows the blocked syscall and its result;
5. `/proc/<pid>/status` reports the seccomp mode.

## Prerequisites

- Linux VM with `gcc`, `libseccomp-dev`, `libseccomp` tools
  (`scmp_sys_resolver`), `strace`. No root needed.
- Read: [2. seccomp modes and filters](../../../docs/06-seccomp/02-seccomp-modes-and-filters.md).

## Build

```bash
gcc -Wall -o seccomp_demo seccomp_demo.c -lseccomp
scmp_sys_resolver -a x86_64 uname          # show uname's syscall number on this arch
```

## Experiment

### Part A — Allowed program runs

```bash
./seccomp_demo errno /bin/echo "hello from a filtered process"
```

### Part B — A blocked syscall returns EPERM

`uname` calls the `uname()` syscall, which the filter does **not** allow.

**Predict first.** With the `errno` default, will `uname -a` print anything?
What will its exit status be?

```bash
./seccomp_demo errno /usr/bin/uname -a; echo "exit code: $?"
```

### Part C — A blocked syscall kills the process

```bash
./seccomp_demo kill /usr/bin/uname -a; echo "exit code: $?"
```

**Predict first.** Which exit code indicates death by a signal, and which
signal is it?

### Part D — See it under strace

```bash
strace -f -e trace=uname,seccomp,prctl ./seccomp_demo errno /usr/bin/uname -a 2>&1 | grep -E 'seccomp|prctl|uname'
```

### Part E — The seccomp mode in /proc

```bash
./seccomp_demo errno /bin/sh -c 'grep Seccomp /proc/self/status; echo "---"; uname -a; echo "uname exit: $?"'
```

### Part F — The filter is inherited by children

```bash
./seccomp_demo errno /bin/sh -c 'echo "shell ok"; /usr/bin/uname -a; echo "child uname exit: $?"'
```

## Expected observations

**Part A.** Prints `hello from a filtered process`.

**Part B.** `uname` prints nothing (or a partial/error line) and fails. Because
`uname()` returned `-1 EPERM`, the tool reports an error such as
`uname: cannot get system name: Operation not permitted`; exit code non-zero
(typically 1).

**Part C.** No output from `uname`; the shell reports the process was killed. The
exit code is **159** (128 + 31): `SIGSYS`, the signal seccomp uses for a killed
syscall. (Some shells print `Bad system call`.)

**Part D.** The trace shows `prctl(PR_SET_NO_NEW_PRIVS, 1) = 0`, then
`seccomp(SECCOMP_SET_MODE_FILTER, ...) = 0`, then in the exec'd process
`uname(...) = -1 EPERM (Operation not permitted)`.

**Part E.** `Seccomp: 2` (2 = filter mode). `uname -a` fails with
`Operation not permitted`, `uname exit` non-zero, but the surrounding shell keeps
running.

**Part F.** `shell ok` prints, then `uname` fails: the child `uname` process
inherited the filter across `fork()`+`execve()`.

## Why this happens

- `seccomp_load()` set `no_new_privs` and installed a cBPF program via
  `seccomp(SECCOMP_SET_MODE_FILTER, ...)`. No capability was needed.
- The default action (`SCMP_ACT_ERRNO(EPERM)` or `SCMP_ACT_KILL_PROCESS`) applies
  to every syscall not in the allow list, evaluated on each call.
- The filter is part of the task and is copied to children and kept across
  `execve()`, so the exec'd `uname` runs under it.
- `SCMP_ACT_KILL_PROCESS` terminates the whole thread group with `SIGSYS`
  (128 + 31 = 159).

## Connection to containers

- This is exactly how a runtime applies a container's seccomp profile: build a
  filter with libseccomp, set `no_new_privs`, `seccomp()`, then `execve()` the
  application.
- Part B is why a blocked syscall in a container looks like a normal `EPERM`
  failure; the default Docker profile uses `errno` (specifically returning
  `EPERM`/`ENOSYS`), not kill, so applications get an error rather than dying.
- Part C shows the alternative (`SCMP_ACT_KILL_PROCESS`), which some strict
  profiles use.

## Questions to think about

1. Why does the allow list need `execve`, `mmap`, `openat`, and `read` just to
   run `/bin/echo`? (Recall Chapter 01 Lab 01: what happens before `main`.)
2. Why is returning `EPERM` often better for compatibility than killing the
   process? When would killing be the safer choice?
3. A container works on one host but crashes with `Bad system call` on another.
   How could the kernel version and the seccomp profile interact to cause this?
4. How would you extend `seccomp_demo.c` to allow `uname` only, but still block,
   say, `mount`?
