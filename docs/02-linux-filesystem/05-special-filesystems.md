# 5. Special Filesystems a Process Expects

## The problem: a root filesystem is not just files on disk

Copy a Linux distribution's files into a directory and make it the root of a
process. Many programs will immediately fail:

- `ps` finds no processes, because `/proc` is an empty directory;
- `echo hi > /dev/null` creates a regular file called `null`, or fails;
- a program that asks for a terminal cannot open `/dev/pts/0`;
- the JVM cannot read the cgroup limits under `/sys/fs/cgroup`;
- programs writing to `/tmp` or `/dev/shm` may fill a disk or fail.

Much of what a Linux process expects under `/` is not stored on disk at all. It
is provided by **special filesystems** (also called virtual or pseudo
filesystems) that are *mounted* at conventional locations. A container runtime
must create these mounts for every container. This section explains what each
one is.

## The standard set

| Mount point | Filesystem type | What it provides | Isolation concern |
|---|---|---|---|
| `/proc` | `proc` | process and kernel information (Chapter 01 §4) | shows processes of a PID namespace; exposes kernel tunables in `/proc/sys` |
| `/sys` | `sysfs` | kernel objects: devices, drivers, kernel modules, firmware | mostly host-global; usually read-only in containers |
| `/sys/fs/cgroup` | `cgroup2` | the cgroup v2 hierarchy (Chapter 04) | should expose only the container's own cgroup |
| `/dev` | `devtmpfs` on hosts; `tmpfs` in containers | device nodes | which devices a process can reach |
| `/dev/pts` | `devpts` | pseudo-terminals | a separate instance per container |
| `/dev/shm` | `tmpfs` | POSIX shared memory | size limit, IPC isolation (Chapter 03) |
| `/dev/mqueue` | `mqueue` | POSIX message queues | IPC namespace (Chapter 03) |
| `/tmp`, `/run` | `tmpfs` (often) | memory-backed scratch space | memory usage counts toward cgroup limits |

## procfs and sysfs: kernel interfaces as files

You met **procfs** in Chapter 01. Two further points matter here:

- `/proc/sys` contains **sysctl** tunables. Writing
  `/proc/sys/net/ipv4/ip_forward` changes kernel behavior. Some are
  per-namespace (for example most of `net.*` belongs to a network namespace);
  many are global for the whole host (for example `kernel.*` and `vm.*`
  settings). A container that can write global sysctls can affect the host.
- A procfs instance is tied to a **PID namespace** when it is mounted. Mounting
  a new procfs after entering a new PID namespace is what makes `ps` show only
  the container's processes (Chapter 03).

**sysfs** exposes the kernel's device model (`/sys/class/net`, `/sys/block`,
`/sys/devices`). Most of it describes host hardware. Writable sysfs attributes
can, for example, unbind drivers or change device parameters, so container
runtimes normally mount it read-only.

## Device files and `/dev`

A **device file** (device node) is an inode that does not store data. Instead
it carries a type (**character** or **block**) and a pair of numbers
(**major**, **minor**) that select a driver in the kernel:

```console
$ ls -l /dev/null /dev/zero /dev/vda
crw-rw-rw- 1 root root   1, 3 ... /dev/null      c = character device, major 1, minor 3
crw-rw-rw- 1 root root   1, 5 ... /dev/zero
brw-rw---- 1 root disk 252, 0 ... /dev/vda       b = block device
```

Opening `/dev/null` does not read the name "null". The kernel looks at the
inode's type and (1, 3) and dispatches to the memory driver's "null" device.
Consequences:

- **The name and location do not matter.** A device node with (252, 0) created
  anywhere with any name gives access to the same disk, subject to its
  permission bits.
- **Creating device nodes is privileged.** `mknod()` requires the
  `CAP_MKNOD` capability.
- **`nodev` blocks use.** A device node on a filesystem mounted `nodev` cannot
  be opened (Chapter 02 §2).
- **cgroups can block use.** The cgroup device controller (Chapter 04) can deny
  open/read/write of specific majors and minors, even for root.

On a host, `/dev` is a **devtmpfs** instance, which the kernel populates
automatically with a node for every device it detects, and `udev` adjusts
names and permissions. A container must **not** receive the host's devtmpfs,
because it contains nodes for every host disk. Runtimes instead mount an empty
`tmpfs` at `/dev` and create (or bind-mount) only a minimal set:

```text
/dev/null  /dev/zero  /dev/full  /dev/random  /dev/urandom  /dev/tty
/dev/console (a pty)   /dev/pts/  /dev/ptmx -> pts/ptmx   /dev/shm/  /dev/mqueue/
/dev/fd -> /proc/self/fd   /dev/stdin -> /proc/self/fd/0   (and stdout, stderr)
```

This list is almost exactly the "default devices" of the OCI runtime
specification (Chapter 10).

## devpts: terminals

When you SSH into a machine or run `docker run -it`, the program sees a
**terminal**. Modern terminals are **pseudo-terminals (ptys)**: a pair of
connected devices. The *master* side is held by the program providing the
terminal (`sshd`, a terminal emulator, or a container shim); the *slave* side,
`/dev/pts/N`, becomes the stdin/stdout/stderr of the shell.

The `devpts` filesystem creates these `/dev/pts/N` nodes. Mounting a **new
instance** of devpts (the `newinstance` behavior is the default since Linux
4.7) gives an isolated set of pty numbers, so a container cannot open the
host's terminals. `/dev/ptmx` is the multiplexer used to allocate a new pty
pair.

## tmpfs: memory-backed files

**tmpfs** stores files in the kernel's page cache and swap. Options include
`size=` (maximum size) and `mode=`. Two facts matter for containers:

- tmpfs memory is **charged to the memory cgroup** of the process that first
  touched the pages. Writing 1 GB into `/dev/shm` inside a container with a
  512 MB memory limit can trigger the OOM killer (Chapter 04).
- `/dev/shm` is used by POSIX shared memory (`shm_open()`). The JVM and
  browsers use it; the Docker default size of 64 MB is a common source of
  errors in those applications.

## cgroup2 filesystem

The **cgroup2** filesystem at `/sys/fs/cgroup` is the entire interface to cgroups:
directories are groups, and files are settings and statistics
(`memory.max`, `cpu.max`, `cgroup.procs`). There is no separate "cgroup
syscall" for most operations. Chapter 04 covers it. For this chapter, note only
that it is a mounted filesystem, so what a process sees depends on what the
runtime mounts, together with the cgroup namespace (Chapter 03).

## Why this matters for containers

A container runtime prepares a root filesystem in roughly this order:

```text
rootfs (image directory)   mounted at the new root
  ├── /proc          new proc instance          (after the PID namespace exists)
  ├── /dev           new tmpfs, mode=755
  │     ├── null, zero, full, random, urandom, tty   device nodes or bind mounts
  │     ├── pts      new devpts instance
  │     ├── shm      new tmpfs, size limited
  │     └── mqueue   new mqueue instance         (after the IPC namespace exists)
  ├── /sys           sysfs, read-only
  │     └── fs/cgroup  cgroup2, usually read-only
  └── volumes, /etc/hosts, /etc/resolv.conf     bind mounts
then: mask sensitive /proc paths, make others read-only, pivot_root
```

Every item is one `mount()` or `mknod()` call from this chapter. If you run
`cat /proc/<container-pid>/mountinfo` on any host with containers, you will
now recognize every line.

## Evidence

Lab: [`lab-05-special-filesystems`](../../labs/02-linux-filesystem/lab-05-special-filesystems/)

## Further Reading

- Kernel docs: [Tmpfs](https://docs.kernel.org/filesystems/tmpfs.html)
  and [Devpts Filesystem](https://docs.kernel.org/filesystems/devpts.html)
  — the tmpfs options (`size`, `nr_inodes`, `mode`) and the devpts
  multi-instance semantics.
- Kernel docs: [sysfs — The filesystem for exporting kernel objects](https://docs.kernel.org/filesystems/sysfs.html)
  — why sysfs exists and how its directories map to kernel objects.
- Kernel docs: [Documentation for /proc/sys](https://docs.kernel.org/admin-guide/sysctl/index.html)
  — the tunables under `/proc/sys`; skim `kernel.rst` to see how much global
  state is exposed there.
- [`mknod(2)`](https://man7.org/linux/man-pages/man2/mknod.2.html) and
  [`makedev(3)`](https://man7.org/linux/man-pages/man3/makedev.3.html) — how
  device nodes are created and identified.
- [`pty(7)`](https://man7.org/linux/man-pages/man7/pty.7.html) — master/slave
  pseudo-terminal pairs, needed to understand `-t` and `exec -it` later.
- Kernel docs: [Linux allocated devices](https://docs.kernel.org/admin-guide/devices.html)
  — the registry of major/minor numbers; look up `1 char` to see
  `/dev/null`, `/dev/zero`, and others.
- OCI Runtime Specification, [Default Devices](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#default-devices)
  and [Default Filesystems](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#default-filesystems)
  — a short preview of Chapter 10 that matches the lists in this section.
