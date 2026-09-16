# 2. Mounts and the Mount Table

## The problem: attaching filesystems to one tree

The VFS has many filesystem instances, each with its own internal tree. Users
and programs need a single namespace of paths. Something must record "the
filesystem on `/dev/vdb1` appears at `/data`" and "a procfs instance appears at
`/proc`".

## The Linux abstraction: a mount

A **mount** is a kernel object (`struct mount`) that says:

> Show *this dentry* of *this filesystem instance* at *this mount point* (a
> dentry within a parent mount).

The collection of mounts forms the **mount tree**: every mount except the root
mount has a parent mount. Path resolution walks this tree, as described in
[section 1](01-vfs-and-path-resolution.md).

```text
mount tree                              what a path lookup sees
──────────                              ───────────────────────
[1] ext4 /dev/vda1   at /               /
 ├─[2] proc          at /proc           ├── proc/      → procfs root
 ├─[3] sysfs         at /sys            ├── sys/       → sysfs root
 │   └─[4] cgroup2   at /sys/fs/cgroup  │   └── fs/cgroup/ → cgroup2 root
 ├─[5] tmpfs         at /tmp            ├── tmp/       → a tmpfs instance
 └─[6] ext4 /dev/vdb1 at /data          └── data/      → another ext4 instance
```

Four details matter for everything that follows:

1. **A mount attaches a *dentry*, not necessarily a filesystem root.** Usually it
   is the root of the filesystem, but bind mounts ([section 3](03-bind-mounts.md))
   attach a subdirectory or even a single file.
2. **One filesystem instance can be mounted many times.** Each mount is a
   separate `struct mount` pointing to the same `super_block`.
3. **Mounts stack.** Mounting on a directory that is already a mount point
   places the new mount on top; only the top one is visible.
4. **Mounting hides the directory's previous contents** without deleting them.
   A process that already had a file descriptor or working directory inside the
   hidden directory keeps using it.

## The system call

```c
#include <sys/mount.h>
int mount(const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data);
int umount2(const char *target, int flags);
```

| Argument | Meaning | Example |
|---|---|---|
| `source` | What to mount: a device, a directory (for bind), or a placeholder name for virtual filesystems | `/dev/vdb1`, `proc`, `tmpfs` |
| `target` | The mount point directory (or file) | `/data` |
| `filesystemtype` | Which filesystem driver creates the instance | `ext4`, `proc`, `tmpfs`, `overlay` |
| `mountflags` | Generic flags handled by the VFS | `MS_RDONLY`, `MS_NOSUID`, `MS_BIND`, `MS_REMOUNT`, `MS_PRIVATE` |
| `data` | Filesystem-specific options as a string | `"size=64m"` for tmpfs, `"lowerdir=...,upperdir=..."` for overlay |

`mount()` requires `CAP_SYS_ADMIN` in the user namespace that owns the
caller's mount namespace. For now, read that as "requires root" (Chapter 05
explains the exact rule).

The same system call does several different jobs depending on flags:

| Flags | Operation |
|---|---|
| none of the below | create a new mount of a filesystem |
| `MS_BIND` (+ `MS_REC`) | bind mount ([section 3](03-bind-mounts.md)) |
| `MS_REMOUNT` | change options of an existing mount |
| `MS_SHARED`, `MS_PRIVATE`, `MS_SLAVE`, `MS_UNBINDABLE` (+ `MS_REC`) | change propagation type ([section 4](04-mount-propagation.md)) |
| `MS_MOVE` | move an existing mount to another place |

`umount2()` detaches a mount. It fails with `EBUSY` if the mount is in use
(open files, working directories, child mounts). The flag `MNT_DETACH` performs a
**lazy unmount**: the mount disappears from the tree immediately, and is freed
when the last user goes away. Container runtimes use this when discarding the
old root (Chapter 07).

### The newer mount API

Since Linux 5.2 there is a second, file-descriptor-based API: `fsopen()`,
`fsconfig()`, `fsmount()`, `move_mount()`, and `open_tree()`, followed by
`mount_setattr()` in 5.12. It separates "create a filesystem context",
"configure it", and "attach it somewhere", and lets a mount exist as a file
descriptor before being attached. Container runtimes increasingly use it,
because it avoids path-based races and enables features such as idmapped
mounts (Chapter 05). The concepts in this chapter apply to both APIs.

## Per-mount options vs filesystem options

Some options belong to the **mount** (the `struct mount`), and some to the
**filesystem instance** (the `super_block`):

| Per-mount (VFS) | Meaning |
|---|---|
| `ro` / `rw` | whether writes through *this mount* are allowed |
| `nosuid` | ignore set-user-ID/set-group-ID bits (and file capabilities) on `execve()` |
| `nodev` | do not allow opening device files through this mount |
| `noexec` | do not allow `execve()` of files through this mount |
| `noatime`, `relatime` | access-time update policy |

Filesystem-specific options (`size=` for tmpfs, `errors=` for ext4) belong to
the super block and are shared by every mount of that instance.

The distinction explains why one directory can be writable in one place and
read-only in another (a read-only bind mount, section 3), and why a container
runtime can mount a host path into a container with `nosuid,nodev` even though
the host mount has neither.

## Reading the mount table: `/proc/<pid>/mountinfo`

The kernel exposes the mount tree **as seen by a specific process** in
`/proc/<pid>/mountinfo`. (`/proc/mounts` and `/etc/mtab` are older, less
detailed formats; on modern systems `/proc/mounts` is a symlink to
`/proc/self/mounts`.)

```text
36 25 0:32 / /tmp rw,nosuid,nodev shared:17 - tmpfs tmpfs rw,size=1996116k,inode64
│  │  │    │ │    │               │         │ │     │     │
│  │  │    │ │    │               │         │ │     │     └ (11) super block options
│  │  │    │ │    │               │         │ │     └ (10) source
│  │  │    │ │    │               │         │ └ (9) filesystem type
│  │  │    │ │    │               │         └ (8) separator
│  │  │    │ │    │               └ (7) optional fields: propagation (section 4)
│  │  │    │ │    └ (6) per-mount options
│  │  │    │ └ (5) mount point, relative to the process's root directory
│  │  │    └ (4) root: which directory of the filesystem is mounted here
│  │  └ (3) major:minor device ID of the filesystem instance
│  └ (2) parent mount ID
└ (1) mount ID
```

Fields 4 and 5 are the ones people most often misread:

- **Field 4 (root)** is `/` for an ordinary mount. For a bind mount of
  `/srv/data/app` it is `/data/app`, the path *inside the source filesystem*.
- **Field 5 (mount point)** is shown relative to the reading process's root
  directory. A process with a different root sees different paths for the
  same mounts.

`findmnt` formats this file as a tree and is the most convenient tool:

```bash
findmnt                          # tree of all mounts
findmnt -T /some/path            # which mount contains this path
findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS,PROPAGATION
```

### Why the mount table is per process

If there were a single global mount table, `/proc/self/mountinfo` would not
need `self`. It is per process because every process belongs to a **mount
namespace**, and each mount namespace has its own mount tree. On a host
without containers, nearly every process is in the same mount namespace, so
they all see the same table. Chapter 03 creates a second one.

## Why this matters for containers

- A container's view of the filesystem is a mount tree: the image at `/`, then
  `proc` at `/proc`, `sysfs` at `/sys`, a `tmpfs` at `/dev`, `devpts` at
  `/dev/pts`, `mqueue` at `/dev/mqueue`, bind mounts for volumes and for
  `/etc/hosts`, `/etc/hostname`, and `/etc/resolv.conf`. You can read it from
  the host with `cat /proc/<container-pid>/mountinfo`.
- The OCI runtime configuration has a `mounts` array whose entries have exactly
  the `mount()` arguments from this section: `destination` (target), `type`,
  `source`, and `options` (Chapter 10).
- Per-mount options such as `nosuid`, `nodev`, `noexec`, and `ro` are a basic
  container hardening tool.

## Evidence

Lab: [`lab-02-mounts-and-mountinfo`](../../labs/02-linux-filesystem/lab-02-mounts-and-mountinfo/)

## Further Reading

- [`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html) — the
  authoritative list of `MS_*` flags and the rules for combining them. Read the
  sections "Remounting an existing mount", "Creating a bind mount", and
  "Changing the propagation type".
- [`proc_pid_mountinfo(5)`](https://man7.org/linux/man-pages/man5/proc_pid_mountinfo.5.html)
  — the field-by-field specification of `mountinfo`.
- [`mount(8)`](https://man7.org/linux/man-pages/man8/mount.8.html) — the
  command-line tool; the "FILESYSTEM-INDEPENDENT MOUNT OPTIONS" section defines
  `nosuid`, `nodev`, `noexec`, and friends.
- [`findmnt(8)`](https://man7.org/linux/man-pages/man8/findmnt.8.html) — the
  most useful inspection tool for every filesystem chapter in this repository.
- LWN, David Howells' new mount API series, starting with
  ["A new API for mounting filesystems"](https://lwn.net/Articles/753473/)
  (2018) — the design motivation for `fsopen()`/`fsmount()`/`move_mount()`.
  Useful before reading modern runc mount code.
- Kernel source: [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c)
  — `path_mount()` is the dispatcher that turns `mount()` flags into
  `do_remount()`, `do_loopback()` (bind), `do_change_type()` (propagation),
  `do_move_mount()`, or `do_new_mount()`. It maps exactly onto the operations
  table above.
