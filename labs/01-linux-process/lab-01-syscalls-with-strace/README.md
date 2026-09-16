# Lab 01 — Observing System Calls with strace

## Goal

Establish, with direct evidence, that:

1. programs interact with the kernel only through system calls;
2. libc function names are not always the same as syscall names;
3. a failing syscall is reported by the kernel as an error number that libc
   turns into `-1` plus `errno`;
4. not every library call becomes a syscall.

## Prerequisites

- Linux VM, any recent distribution.
- Packages: `strace`, `gcc` (`build-essential` on Debian/Ubuntu).
- Read: [1. Processes and system calls](../../../docs/01-linux-process/01-processes-and-system-calls.md).

No root access is needed.

## Background

`strace` uses the `ptrace()` system call to stop the traced process at every
syscall entry and exit, and prints the syscall name, decoded arguments, and
return value. It shows the boundary between user mode and kernel mode.

## Experiment

### Part A — A familiar command, seen from the kernel

**Predict first.** How many different syscalls do you think `cat /etc/hostname`
makes? Five? Twenty? Write your guess down.

```bash
strace -c cat /etc/hostname
```

`-c` prints a summary table instead of each call. Now see the actual calls:

```bash
strace cat /etc/hostname 2>&1 | tail -n 15
```

`strace` writes its trace to standard error, so `2>&1` merges it into the pipe.

### Part B — libc names vs syscall names

```bash
gcc -Wall -o hello_raw hello_raw.c
./hello_raw
strace -e trace=write,openat ./hello_raw
```

**Predict first.** The C code calls `open()`. Will strace show `open`?

### Part C — A syscall that fails

This is already part of `hello_raw` (step 4). Also try from the shell:

```bash
strace -e trace=openat cat /no/such/file
```

### Part D — A call that does not enter the kernel

```bash
strace -f -e trace=clock_gettime,gettimeofday,time date
```

**Predict first.** `date` must obtain the current time. Will you see a
`clock_gettime` syscall?

Then look for the vDSO in the process memory map:

```bash
grep vdso /proc/self/maps
```

## Expected observations

**Part A.** The summary shows roughly 20–40 syscalls of maybe 15–25 distinct
types. Most are not about reading the file at all: `execve` (start the
program), `brk` and `mmap` (memory setup), `openat` of `/etc/ld.so.cache` and
`libc.so.6` (the dynamic loader loading libc), `mprotect`, `arch_prctl` or
`set_tid_address` (runtime setup). The actual work is a small tail:

```text
openat(AT_FDCWD, "/etc/hostname", O_RDONLY) = 3
fstat(3, ...) = 0
read(3, "lab\n", 131072) = 4
write(1, "lab\n", 4) = 4
read(3, "", 131072) = 0
close(3) = 0
```

Exact buffer sizes and helper calls such as `fadvise64` vary by coreutils
version.

**Part B.** The program prints four lines. The trace shows three `write(1, ...)`
calls that are identical at the syscall level, although the C code used three
different functions. The `open()` call appears as:

```text
openat(AT_FDCWD, "/this/path/does/not/exist", O_RDONLY) = -1 ENOENT (No such file or directory)
```

There is no `open` syscall in the trace, even though the code called `open()`.

**Part C.** `openat(...) = -1 ENOENT`, and `hello_raw` prints
`errno=2 (No such file or directory)`.

**Part D.** On most x86-64 and arm64 systems, **no** `clock_gettime` syscall
appears, yet `date` prints the correct time. `/proc/self/maps` contains a line
ending in `[vdso]`.

## Why this happens

- The dynamic loader and libc initialization run *before* `main()`. They need
  memory mappings and shared libraries, and all of that requires syscalls.
- glibc implements `open()` with the `openat` syscall, using the special
  directory file descriptor `AT_FDCWD` ("relative to the current working
  directory"). The C API stayed stable while the kernel interface evolved.
- `printf`, `write()`, and `syscall(SYS_write, ...)` are three layers of
  user-space convenience over one kernel entry point.
- The kernel returns a negative error number. glibc's wrapper converts it to
  `-1` and stores the positive number in `errno`. `strace` decodes the number
  into the symbolic name `ENOENT`.
- `clock_gettime` is implemented in the vDSO: code the kernel maps into every
  process that reads time data from a page shared with the kernel, without
  switching to kernel mode.

## Connection to containers

- When you run `strace` on a containerized process later, you will see the same
  syscalls. There is no container-specific syscall for "read a file".
- The path `/etc/hostname` in Part A is resolved by the kernel using the
  process's root directory and mount namespace. In a container, the same
  `openat` returns a different file. That is Chapters 02, 03, and 07.
- Container runtimes restrict syscalls with seccomp (Chapter 06). A blocked
  syscall typically fails with `-1 EPERM`, exactly like the `ENOENT` failure
  you observed here. Now you know how to recognize it in a trace.

## Questions to think about

1. If a Go program opens a file, will `strace` show glibc involvement? Why or
   why not? (Hint: section "The layers between your code and the kernel".)
2. The vDSO bypasses the syscall boundary. Why is that safe for reading time,
   but would not be safe for reading a file?
3. `strace` itself relies on a syscall (`ptrace`). What might happen if a
   container runtime blocked `ptrace` with seccomp and you tried to `strace` a
   process from *inside* that container?
4. Run `strace -f java -version` (if Java is installed) and count the
   `clone`/`clone3` calls. What do they represent?
