# Labs — Chapter 07: rootfs, chroot, and pivot_root

Work through these in order; each builds on the rootfs created in Lab 01.

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-build-a-rootfs](lab-01-build-a-rootfs/) | build a BusyBox rootfs; the missing-library failure | Yes (chroot) | [§1](../../docs/07-rootfs-chroot-pivot-root/01-what-is-a-rootfs.md) |
| [lab-02-chroot-and-escape](lab-02-chroot-and-escape/) | chroot use, non-isolation, the classic escape | Yes | [§2](../../docs/07-rootfs-chroot-pivot-root/02-chroot.md) |
| [lab-03-pivot-root](lab-03-pivot-root/) | pivot_root, detaching the host root, rootless | Partly | [§3](../../docs/07-rootfs-chroot-pivot-root/03-pivot-root.md) |
| [lab-04-full-root](lab-04-full-root/) | proc/dev/sys, masked/read-only paths, real-container comparison | Yes | [§4](../../docs/07-rootfs-chroot-pivot-root/04-assembling-a-new-root.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y busybox-static util-linux gcc
# optional, for the image-comparison parts: podman OR docker; debootstrap
```

Lab 03 Part D and a full run of Lab 04 also work **rootless** inside
`unshare --user --map-root-user ...` where the distribution allows unprivileged
user namespaces (Chapter 03 Lab 07 check). Expected observations were written for
Linux 6.x, util-linux 2.38+, BusyBox 1.35+.
