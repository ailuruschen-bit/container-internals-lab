# 4. /proc: The Kernel's View of a Process

## The problem: kernel state is invisible

The previous sections described `task_struct` fields: parent, credentials,
open files, root directory, namespaces. These live in kernel memory, which a
user-mode program cannot read. How do `ps`, `top`, `lsof`, and container
tools learn anything about processes?

## The Linux abstraction: a filesystem made of kernel data

Linux exposes process information through **procfs**, a *pseudo-filesystem*
normally mounted at `/proc`. Nothing in `/proc` is stored on disk. When you
read `/proc/1234/status`, the kernel's procfs code finds the task with PID
1234, formats some of its fields as text, and returns that text from `read()`.
Every read reflects the state at that moment.

This fits the Unix idea of using the file API for many kinds of objects.
`ps` is not special: it is a program that opens and reads files under `/proc`.
You can verify this with `strace -e trace=openat ps`.

```text
/proc
├── 1/                    one directory per process (named by PID)
├── 1234/
├── self -> 1234          a symlink to the directory of whoever reads it
├── cpuinfo, meminfo ...  system-wide information
└── sys/                  tunable kernel parameters (sysctl)
```

## The most useful files for this repository

Paths below are relative to `/proc/<pid>/`.

| Path | Shows | Used later for |
|---|---|---|
| `status` | Human-readable summary: name, state, PID, PPID, UIDs, GIDs, capabilities, seccomp mode | Almost every chapter |
| `cmdline` | The `argv` passed to `execve()`, separated by NUL bytes | Identifying processes |
| `environ` | The initial `envp`, separated by NUL bytes | Inspecting a container's environment |
| `exe` | Symlink to the executable file | |
| `cwd` | Symlink to the current working directory | |
| `root` | Symlink to the process's root directory | Chapter 07 |
| `fd/` | One symlink per open file descriptor | Section 5 |
| `task/` | One subdirectory per thread (task) | Section 1, Lab 04 |
| `maps` | Memory mappings (executable, libraries, heap, stack, vDSO) | |
| `limits` | Resource limits (`setrlimit`) | |
| `ns/` | One symlink per namespace the process belongs to | Chapter 03 |
| `cgroup` | The cgroup the process belongs to | Chapter 04 |
| `mountinfo` | The mount table as seen by the process | Chapter 02 |

### Reading `status`

```console
$ grep -E '^(Name|State|Tgid|Pid|PPid|Uid|Gid|Groups|Threads|Cap|NoNewPrivs|Seccomp)' /proc/self/status
Name:   grep
State:  R (running)
Tgid:   8123
Pid:    8123
PPid:   8001
Uid:    1000    1000    1000    1000
Gid:    1000    1000    1000    1000
Groups: 4 27 1000
Threads:        1
CapInh: 0000000000000000
CapPrm: 0000000000000000
CapEff: 0000000000000000
CapBnd: 000001ffffffffff
CapAmb: 0000000000000000
NoNewPrivs:     0
Seccomp:        0
```

Notice that **`/proc/self` is the process that opens the file**, which here is
`grep`, not your shell.

At this point you can interpret the first half. The four `Uid` columns are
real, effective, saved, and filesystem UID ([section 7](07-credentials.md)).
The `Cap*` lines are capability sets (Chapter 05). `NoNewPrivs` and `Seccomp`
are covered in Chapter 06. Newer kernels print extra lines such as
`NSpid` and `Seccomp_filters`. You will use this one file to verify the
effect of almost every container mechanism in this repository.

### NUL-separated files

`cmdline` and `environ` preserve the exact bytes that were passed to
`execve()`, so arguments containing spaces are unambiguous. Arguments are
separated by the byte `\0`:

```bash
tr '\0' '\n' < /proc/$$/cmdline
tr '\0' '\n' < /proc/$$/environ
```

`environ` shows the environment **as it was at `execve()` time**. If the
program later calls `setenv()`, that changes user-space memory, but `environ`
may not reflect it.

### Magic links: `exe`, `cwd`, `root`, `fd/*`

These look like symbolic links, and `readlink` prints a path. But opening them
does not follow that printed path. The kernel resolves them directly to the
object the process actually holds. They are therefore called **magic links**.

Two consequences are important for containers:

- If a process's executable is deleted, `ls -l /proc/<pid>/exe` shows
  `... (deleted)`, but `cat /proc/<pid>/exe > copy` still recovers it.
- `/proc/<pid>/root` resolves to *that process's* root directory. From the
  host, `ls /proc/<container-pid>/root/etc` lists the container's `/etc`, even
  though the path string would suggest the host's root. This is a very
  practical debugging tool once you reach Chapter 07.

## Access control

Not everything in `/proc/<pid>` is readable by everyone. `status`, `cmdline`,
and `stat` are generally world-readable. Sensitive entries such as `environ`,
`fd/`, `maps`, `root`, and `cwd` require that you are allowed to inspect the
process, roughly "same user, or privileged". The kernel uses the same
permission check as `ptrace()` for many of these (`PTRACE_MODE_READ`). This is
why `sudo` is often needed to inspect processes owned by other users, including
container processes started by a root daemon.

## One important preview

The contents of `/proc` depend on **which PID numbering space the procfs
instance was mounted for**. If you create a new PID namespace but keep the old
`/proc` mounted, `ps` inside it still shows host processes. That is why
container runtimes mount a *new* procfs inside the container. Chapter 03
demonstrates this. For now, simply note that `/proc` is a mounted
filesystem, not a fixed part of the kernel's address space.

## Why this matters for containers

- `/proc/<pid>/ns/`, `/proc/<pid>/cgroup`, `/proc/<pid>/status`, and
  `/proc/<pid>/root` are how you will *prove* that a process is isolated. Each
  later lab uses them as evidence.
- Tools such as `nsenter` locate a container's namespaces through
  `/proc/<pid>/ns/*`. runc also uses `/proc/self/...` paths extensively during
  setup.
- Because `/proc` exposes sensitive kernel information, container runtimes
  mask or make read-only several `/proc` paths (for example `/proc/kcore` and
  `/proc/sys`). You will see these as `maskedPaths` and `readonlyPaths` in the
  OCI configuration (Chapter 10).

## Evidence

Lab: [`lab-05-inspecting-proc`](../../labs/01-linux-process/lab-05-inspecting-proc/)

## Further Reading

- [`proc(5)`](https://man7.org/linux/man-pages/man5/proc.5.html) and the
  per-file pages such as
  [`proc_pid_status(5)`](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
  and [`proc_pid_root(5)`](https://man7.org/linux/man-pages/man5/proc_pid_root.5.html)
  — the reference for every field. Use them as a dictionary, not a book.
- Kernel docs: [The /proc Filesystem](https://docs.kernel.org/filesystems/proc.html)
  — section 1.1 "Process-Specific Subdirectories" contains a field-by-field table
  of `status`, maintained alongside the kernel code.
- Kernel source: [`fs/proc/base.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/proc/base.c),
  the array `tgid_base_stuff[]` — the actual list of entries in each
  `/proc/<pid>` directory, with the function that generates each one. Reading
  this array for five minutes makes procfs feel much less mysterious.
