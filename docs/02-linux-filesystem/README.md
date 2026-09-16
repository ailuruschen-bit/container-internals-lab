# Chapter 02 — Linux Filesystem: VFS, Mounts, and the Root Directory

## Why this chapter exists

When a process inside a container opens `/etc/os-release`, it gets the file
from the container image, not the host's file. Chapter 01 showed that the
application made an ordinary `openat()` call. So the difference must be in
**how the kernel resolves a path for that process**.

Path resolution depends on three things this chapter explains:

1. the **Virtual File System (VFS)**, the kernel layer that turns a path into a
   file on some filesystem;
2. **mounts**, which attach filesystems to directories and form a *mount
   tree*;
3. the process's **root directory** and **working directory**, the starting
   points of every path lookup.

Containers change all three: they give a process its own copy of the mount
tree (mount namespace, Chapter 03), attach an image filesystem (OverlayFS,
Chapter 08), and move the process's root into it (`pivot_root`, Chapter 07).
None of that can be understood without first understanding mounts on an
ordinary host.

The chapter also introduces **mount propagation**, one of the most confusing
topics in container filesystems. We learn it here, on a single host, without
namespaces, so that Chapter 03 only needs to add one new idea.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [The Virtual File System and path resolution](01-vfs-and-path-resolution.md) | One path API over many filesystems; lookup starts at the process's root or cwd. |
| 2 | [Mounts and the mount table](02-mounts.md) | A mount attaches a filesystem instance to a directory; `/proc/<pid>/mountinfo` shows the tree. |
| 3 | [Bind mounts](03-bind-mounts.md) | Make an existing directory or file visible at a second location. |
| 4 | [Mount propagation](04-mount-propagation.md) | Shared, private, slave, and unbindable mounts: how mount events spread. |
| 5 | [Special filesystems a process expects](05-special-filesystems.md) | `proc`, `sysfs`, `tmpfs`, `devtmpfs`, `devpts`, `cgroup2`, and device nodes. |
| 6 | [Chapter summary](06-summary.md) | What a container runtime must build for a new root filesystem. |

Labs: [`labs/02-linux-filesystem/`](../../labs/02-linux-filesystem/).
References: [`references/02-linux-filesystem.md`](../../references/02-linux-filesystem.md).

## Prerequisites

- [Chapter 01](../01-linux-process/), especially system calls (§1), `/proc`
  (§4), file descriptors (§5), and credentials (§7).

## Environment

Most labs in this chapter need `sudo`, because `mount()` requires privilege
(the `CAP_SYS_ADMIN` capability). Every lab works inside a scratch directory
under `/tmp` and cleans up after itself, but run them in a disposable VM.
