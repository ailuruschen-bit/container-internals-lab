# Chapter 07 — rootfs, chroot, and pivot_root

## Why this chapter exists

Chapter 03 §4 showed that a new mount namespace starts as a **copy of the host's
mount tree**, so a process in it still sees the host's `/`. Chapter 02 §1 showed
that every absolute path starts at the process's **root directory**. To give a
container "its own `/`", two things must happen:

1. a **root filesystem (rootfs)** must exist: a directory tree containing the
   files the container should see as `/`;
2. the process's **root directory** must be changed to point at that tree, and
   the host's filesystem must be made unreachable.

This chapter covers the mechanisms: `chroot()` (and why it is not enough for
isolation), `pivot_root()` (what runtimes actually use), and the mounts a new
root needs before a process can run in it. It is the last Linux primitive before
Chapter 09 assembles a container by hand.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [What a root filesystem is](01-what-is-a-rootfs.md) | A directory tree that can serve as `/`; how to obtain one without Docker. |
| 2 | [chroot and why it is not a boundary](02-chroot.md) | Changing the root directory; the classic escapes. |
| 3 | [pivot_root](03-pivot-root.md) | Swapping the root mount and detaching the old one. |
| 4 | [Assembling a new root](04-assembling-a-new-root.md) | rootfs + proc/dev/sys + masked paths + read-only, in order. |
| 5 | [Chapter summary](05-summary.md) | What changed, what did not. |

Labs: [`labs/07-rootfs-chroot-pivot-root/`](../../labs/07-rootfs-chroot-pivot-root/).
References: [`references/07-rootfs-chroot-pivot-root.md`](../../references/07-rootfs-chroot-pivot-root.md).

## Prerequisites

- [Chapter 02](../02-linux-filesystem/): VFS and path resolution (§1), mounts
  and bind mounts (§2–3), special filesystems (§5).
- [Chapter 03](../03-namespaces/): mount namespace (§4), user namespace (§7).
- [Chapter 05](../05-capabilities/): `CAP_SYS_CHROOT`, `CAP_SYS_ADMIN`.

## Environment

Linux VM with `sudo`, `util-linux` (`unshare`, `findmnt`), `debootstrap` or
`docker`/`podman` (to obtain a rootfs; alternatives are shown), and a base of
BusyBox for a tiny rootfs. Some labs work unprivileged inside a user namespace.
