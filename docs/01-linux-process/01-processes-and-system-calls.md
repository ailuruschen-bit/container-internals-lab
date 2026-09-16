# 1. Processes and System Calls

## The problem: many programs, one machine

A computer has one set of physical resources: CPU cores, memory, disks, network
interfaces. Many programs want to use them at the same time, and none of them
should be able to corrupt another program's memory or bypass file permissions.

Linux solves this with two ideas that the rest of this repository builds on:

1. **The process** — the kernel's unit of "a running program with its own
   resources".
2. **The system call** — the only controlled doorway from a program into the
   kernel.

## Program vs process

A **program** is a file on disk, for example `/usr/bin/ls` or the `java`
launcher. It is passive: bytes containing machine code and data.

A **process** is a running instance of a program. If three terminals run
`sleep 100`, there is one program and three processes. Each process has its
own memory, its own open files, its own current directory, and its own
identity.

From the kernel's perspective, a process is primarily a **data structure**. In
the Linux source code this structure is `struct task_struct`, defined in
[`include/linux/sched.h`](https://elixir.bootlin.com/linux/v6.12/source/include/linux/sched.h).
It is large (hundreds of fields), but a handful of them explain almost
everything in this repository:

```text
struct task_struct  (simplified; real field names in parentheses)
│
├── identity        process ID (pid), thread group ID (tgid)
├── family          parent (real_parent, parent), children, siblings
├── memory          address space (mm)
├── open files      file descriptor table (files)
├── filesystem ctx  root directory and current directory (fs)
├── credentials     UIDs, GIDs, capabilities (cred, real_cred)
├── signals         handlers (sighand), pending signals, blocked mask
├── namespaces      which isolated "views" the process uses (nsproxy)
├── cgroups         which resource-control groups it belongs to (cgroups)
└── scheduling      state, priority, CPU affinity
```

Keep this picture in mind. Later chapters change exactly these fields:
namespaces change `nsproxy` (and the user namespace referenced from `cred`),
cgroups change `cgroups`, `pivot_root` changes the root in `fs`, capability
dropping changes `cred`, seccomp attaches a filter to the task. None of them
create a new *kind* of object. They modify an ordinary process.

### A note on threads

Linux does not have a completely separate structure for threads. Each thread
is also a `task_struct`. A multithreaded process (a JVM typically has dozens of
threads) is a **thread group**: several tasks that share memory, open files,
and signal handlers.

This causes a naming confusion you will meet in `/proc` and in kernel code:

| Userspace term | Kernel term | Returned by |
|---|---|---|
| Process ID (PID) | Thread group ID (`tgid`) | `getpid()` |
| Thread ID (TID) | Task ID (`pid`) | `gettid()` |

For a single-threaded process, PID and TID are equal. For a JVM, `getpid()`
returns the same value in every thread, while each thread has its own TID.
You can see Java threads as TIDs with `ls /proc/<jvm-pid>/task/`.

## Two privilege levels: user mode and kernel mode

The CPU itself enforces a boundary. On x86-64 and ARM64, code runs either in an
unprivileged **user mode** or a privileged **kernel mode**. In user mode, the
CPU refuses instructions that talk directly to hardware or change memory
mappings. Your application code, the JVM, and libc all run in user mode.

So how does `cat` read a file? It cannot touch the disk controller. It must
ask the kernel.

## System calls: the doorway into the kernel

A **system call** (syscall) is a request from a process to the kernel. On
x86-64 the mechanism looks like this:

1. The program places a **syscall number** in the `rax` register (for example,
   `read` is 0, `write` is 1, `execve` is 59) and arguments in other
   registers.
2. It executes the `syscall` CPU instruction.
3. The CPU switches to kernel mode and jumps to the kernel's syscall entry
   point.
4. The kernel looks up the handler for that number, checks permissions,
   performs the work *on behalf of the current task*, and places a return
   value in `rax`.
5. The CPU returns to user mode, right after the `syscall` instruction.

On failure, the raw kernel return value is a negative error number, such as
`-ENOENT` ("no such file or directory"). The C library converts this into
the familiar convention: the function returns `-1` and sets the thread-local
variable `errno`.

The important phrase is **on behalf of the current task**. When the kernel
handles a syscall, it knows which `task_struct` made the call. Every
permission check, every path lookup, every "which network interfaces exist?"
question is answered *using that task's fields*. This is precisely the hook
that namespaces use: if two tasks point to different namespaces, the same
syscall returns different answers.

## The layers between your code and the kernel

You rarely make system calls directly. There are usually several layers:

```text
Application code          Files.readAllBytes(path)          (Java)
      ↓
Language runtime          JVM native code (C/C++)
      ↓
C library (libc)          open(), read(), close()           (glibc or musl)
      ↓
System call interface     syscall instruction: openat, read, close
      ↓
Linux kernel              VFS → filesystem driver → block layer
      ↓
Hardware / kernel-managed resources
```

A few points that often surprise application engineers:

- **libc function names are not always syscall names.** The C function
  `open()` is implemented with the `openat` syscall on modern glibc. The C
  function `fork()` is implemented with the `clone` syscall. We will see this
  with `strace` in the lab.
- **Not every libc call enters the kernel.** `strlen()` is pure user-space
  computation. `malloc()` usually manages memory in user space and only
  occasionally calls `brk` or `mmap`. Some time functions such as
  `clock_gettime()` are served by the **vDSO**, a small kernel-provided code
  page mapped into every process, specifically to avoid the cost of a real
  syscall.
- **Go does not use libc for most syscalls on Linux.** The Go runtime emits the
  `syscall` instruction itself (package `syscall` and
  `golang.org/x/sys/unix`). This matters later: runc and containerd are written
  in Go, and their low-level operations are thin wrappers around raw syscalls.
- **The JVM is a native program.** `java` is a C/C++ executable using glibc
  (or musl on Alpine). Everything the JVM does with files, threads, sockets,
  and signals eventually becomes a syscall.

## Observing system calls: `strace`

`strace` runs a program and prints every system call it makes, with
arguments and return values. It uses the `ptrace()` syscall, the same
mechanism debuggers use.

```console
$ strace -e trace=openat,read,write,close cat /etc/hostname
openat(AT_FDCWD, "/etc/hostname", O_RDONLY) = 3
read(3, "lab\n", 131072)                  = 4
write(1, "lab\n", 4)                      = 4
lab
read(3, "", 131072)                       = 0
close(3)                                  = 0
```

(Output trimmed; the real trace also shows the dynamic loader opening shared
libraries.)

Read this as a conversation with the kernel:

- "Open `/etc/hostname` for reading." — "OK, it is file descriptor 3."
- "Read up to 131072 bytes from 3." — "Here are 4 bytes."
- "Write 4 bytes to file descriptor 1 (standard output)." — "Written."
- "Read again." — "0 bytes: end of file."

Notice that `cat` never asked *which* `/etc/hostname`. The kernel resolved the
path using the calling task's root directory and mount namespace. Inside a
container, the identical syscall resolves to a different file. Nothing in
`cat` changes.

## Why this matters for containers

- A container runtime is an ordinary program that makes system calls. There is
  no "container syscall". There are syscalls that modify process properties.
- Because every syscall is evaluated against the calling task's properties,
  changing those properties changes what the process sees and can do, without
  modifying the application.
- The kernel is **shared**. Every container on a host makes syscalls into the
  same kernel. This is the fundamental difference from a virtual machine, and
  the reason why the kernel's syscall surface is a security boundary (seccomp,
  Chapter 06).

## Evidence

Lab: [`lab-01-syscalls-with-strace`](../../labs/01-linux-process/lab-01-syscalls-with-strace/)

## Further Reading

- [`syscalls(2)`](https://man7.org/linux/man-pages/man2/syscalls.2.html) —
  the list of Linux system calls with the kernel version that introduced each.
  Worth skimming once to calibrate how small the kernel interface actually is
  compared with libc.
- [`syscall(2)`](https://man7.org/linux/man-pages/man2/syscall.2.html) —
  the table of which registers carry the syscall number and arguments on each
  architecture. This is the precise user/kernel calling convention.
- [`strace(1)`](https://man7.org/linux/man-pages/man1/strace.1.html) —
  the options `-f` (follow children), `-e trace=` (filter), and `-k` (stack
  traces) turn strace into a primary learning tool for this whole repository.
- [`vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html) —
  explains why some calls never appear in strace.
- Kernel source: [`struct task_struct`](https://elixir.bootlin.com/linux/v6.12/source/include/linux/sched.h)
  (search for `struct task_struct {`). Do not read it fully; locate `mm`,
  `files`, `fs`, `cred`, `nsproxy`, and `cgroups` to connect the diagram above
  to real code.
