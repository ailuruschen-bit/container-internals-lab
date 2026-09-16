# 1. What a Root Filesystem Is

## The problem: `/` is just a directory somewhere

Chapter 02 established that `/` is not special to the hardware; it is whatever
directory a process's root points to, and path resolution starts there. So a
"container filesystem" does not require a disk, a partition, or an image format.
It requires a **directory tree that contains everything the container's programs
expect to find under `/`**.

## What a program expects under `/`

Run any dynamically linked program and it immediately needs more than its own
binary:

```console
$ ldd /bin/ls
        linux-vdso.so.1
        libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
        /lib64/ld-linux-x86-64.so.2
```

If `/bin/ls` is placed in a directory that becomes `/`, but
`/lib/x86_64-linux-gnu/libc.so.6` and the dynamic loader
`/lib64/ld-linux-x86-64.so.2` are **not** there, `ls` fails to start with
"No such file or directory" — referring to the missing loader, not to `ls`. A
usable rootfs therefore needs, at minimum:

- the programs to run (`/bin`, `/usr/bin`, `/sbin`);
- their shared libraries and the dynamic loader (`/lib`, `/lib64`, `/usr/lib`);
- configuration the programs read (`/etc`, for example `/etc/passwd`,
  `/etc/nsswitch.conf`, `/etc/resolv.conf`);
- empty mount-point directories for the pseudo-filesystems (`/proc`, `/sys`,
  `/dev`, `/tmp`) that will be mounted later (Chapter 02 §5).

It does **not** need a kernel: the container shares the host's kernel
(Chapter 03). This is why a container image is far smaller than a VM image, and
why `uname -r` in a container shows the host kernel.

## Static vs dynamic binaries

A **statically linked** binary contains all its code and needs no shared
libraries. A rootfs containing only a static binary and empty mount-point
directories is enough to run it. This is the smallest possible rootfs, and it is
how "scratch" container images and Go binaries (which are static by default)
work.

```text
minimal-rootfs/
├── app            (a static binary)
├── proc/          (empty; procfs mounted here later)
├── dev/           (empty; devtmpfs/tmpfs mounted here later)
└── sys/           (empty)
```

**BusyBox** is a single binary that implements `sh`, `ls`, `cat`, `mount`, and
hundreds of other tools; a static BusyBox plus a few directories is a complete,
tiny, interactive rootfs, which the labs use.

## Ways to obtain a rootfs without Docker

| Method | What it gives | Command |
|---|---|---|
| A static binary | the smallest rootfs | `cp $(which busybox) rootfs/bin/ && busybox --install rootfs/bin` |
| `debootstrap` | a full Debian/Ubuntu userland | `sudo debootstrap stable rootfs http://deb.debian.org/debian` |
| Alpine minirootfs | a small distro tarball | download `alpine-minirootfs-*.tar.gz` and `tar -xf` it |
| Export an OCI image | a real image's files, no Docker daemon needed to *use* them | `podman export $(podman create alpine) | tar -x -C rootfs`, or `docker export` |
| Copy from the host | quick but messy | `cp -a /bin /lib ... rootfs/` |

The important point for this repository: an OCI/Docker image's root filesystem is
**exactly this kind of directory tree**, assembled from layers (Chapter 08). A
runtime unpacks the layers into a directory and treats it as the rootfs.

## Ownership and the "who am I" problem

The files in a rootfs have owners (UIDs/GIDs) stored in their inodes
(Chapter 01 §7). A rootfs unpacked as root has files owned by UID 0. Inside the
container:

- without a user namespace, UID 0 in the container matches those files
  naturally;
- with a user namespace, the container's UID 0 maps to a high host UID, so the
  files (owned by real UID 0 on the host, or by the mapped range) must be owned
  accordingly, or an **idmapped mount** must translate them (Chapter 03 §7). This
  is why rootless tools unpack images into a subordinate UID range.

## Why this matters for containers

- A container image is a packaged rootfs plus metadata. Understanding that it is
  "just a directory tree that can be `/`" removes the mystery from images and
  from `FROM scratch`.
- Missing-library errors ("No such file or directory" for a binary that clearly
  exists) are almost always an incomplete rootfs: the loader or a shared library
  is absent. Recognizing this is a common container-debugging skill.
- The `config.json` of an OCI bundle names the rootfs directory in its `root`
  field (Chapter 10).

## Evidence

Lab: [`lab-01-build-a-rootfs`](../../labs/07-rootfs-chroot-pivot-root/lab-01-build-a-rootfs/)

## Further Reading

- [`ldd(1)`](https://man7.org/linux/man-pages/man1/ldd.1.html) and
  [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html) — how a
  dynamic binary finds its loader and libraries; why a rootfs needs them.
- [`debootstrap(8)`](https://manpages.debian.org/stable/debootstrap/debootstrap.8.en.html)
  — building a Debian rootfs from packages.
- [BusyBox](https://www.busybox.net/about.html) — the single-binary userland the
  labs use.
- Filesystem Hierarchy Standard: [refspecs.linuxfoundation.org/FHS_3.0](https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html)
  — what each top-level directory of a Unix rootfs is for.
- OCI Image Specification: [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  — how image layers become a rootfs directory tree (preview of Chapter 08).
