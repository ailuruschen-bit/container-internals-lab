# 5. Chapter Summary: Its Own Root

## The model you should now have

A container's filesystem is a **directory tree (rootfs)** that becomes the
process's `/`. Getting a process into it safely is not `chroot()`; it is a
sequence performed inside a private mount namespace:

```text
new mount namespace ─► make propagation private/slave ─► bind rootfs onto itself
   ─► mount /proc,/sys,/dev,... under it ─► mask and read-only sensitive paths
   ─► pivot_root(new, old) ─► detach old root (lazy umount) ─► drop caps, seccomp, setuid ─► execve
```

`chroot()` changes only the root directory and leaves the host mounted
underneath, so a privileged process can escape it; `pivot_root()` swaps the root
mount and lets the runtime detach the old root entirely.

## Key facts

| Fact | Section |
|---|---|
| A rootfs is a directory tree with binaries, libraries, the loader, `/etc`, and empty mount points | 1 |
| A dynamic binary fails without its loader/libraries present ("No such file or directory") | 1 |
| An OCI image's root filesystem is exactly such a tree, unpacked from layers | 1, and Ch. 08 |
| `chroot()` changes `fs->root` only; not a security boundary | 2 |
| `pivot_root()` needs a mount namespace, private propagation, and a rootfs that is a mount point | 3 |
| Detaching the old root makes the host filesystem unreachable, unlike chroot | 3 |
| A working root also needs proc/dev/sys, masked paths, and read-only paths | 4 |

## The sentence to remember

> A container gets "its own `/`" by mounting an image rootfs inside a private
> mount namespace and `pivot_root`-ing into it, then detaching the host root, so
> the host filesystem is not hidden but **absent** from the container's view.

## What remains

- **Where the rootfs layers come from.** Chapter 07 treated the rootfs as a ready
  directory. Chapter 08 (OverlayFS) explains how image layers are combined into
  one, copy-on-write, so many containers share read-only layers and each gets a
  private writable layer.
- **Assembling everything into one program.** Chapter 09 combines Chapters 01–08
  into a mini container in Go.

## Self-check questions

1. Why does a dynamically linked binary fail in a minimal rootfs even though the
   binary is present? What is actually missing? (§1)
2. Give two ways a privileged process escapes a naive `chroot`, and the mechanism
   each exploits. (§2)
3. List the three `pivot_root` requirements and the operation a runtime performs
   to satisfy each. (§3)
4. After `pivot_root` + lazy unmount, why is the host filesystem *absent* rather
   than merely *hidden*? (§3)
5. Why must `/proc` be mounted after entering the PID namespace, and `/dev/mqueue`
   after the IPC namespace? (§4, Ch. 03)
6. What is the difference between a **masked** path and a **read-only** path, and
   how is each implemented? (§4)
7. Name every mount you would expect in a minimal container's
   `/proc/<pid>/mountinfo` and the chapter that introduced it. (§4)

## Next: Chapter 08 — OverlayFS

Chapter 08 explains how the single rootfs directory of this chapter is actually
built from stacked, read-only image layers plus one writable layer, using
OverlayFS: `lowerdir`, `upperdir`, `workdir`, and `merged`, copy-up, and
whiteouts, all built by hand before connecting them to container image layers.
