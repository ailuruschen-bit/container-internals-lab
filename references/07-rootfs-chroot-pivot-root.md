# References — Chapter 07: rootfs, chroot, and pivot_root

Version note: kernel source pinned to Linux **v6.12**; runc source on `main`.

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`chroot(2)`](https://man7.org/linux/man-pages/man2/chroot.2.html) | The syscall and the explicit note that it is not an escape-proof boundary. | §2 |
| [`pivot_root(2)`](https://man7.org/linux/man-pages/man2/pivot_root.2.html) | Requirements, `EINVAL` cases, shared-mount rules, the `.`/relative idiom. The core reference for §3. | §3 |
| [`pivot_root(8)`](https://man7.org/linux/man-pages/man8/pivot_root.8.html), [`chroot(1)`](https://man7.org/linux/man-pages/man1/chroot.1.html) | The command-line tools in the labs. | Labs |
| [`path_resolution(7)`](https://man7.org/linux/man-pages/man7/path_resolution.7.html) | How the root directory bounds `..` (the chroot escape). | §2 |
| [`ldd(1)`](https://man7.org/linux/man-pages/man1/ldd.1.html), [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html) | Why a rootfs needs the loader and libraries. | §1 |
| [`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html), [`mount_namespaces(7)`](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html) | Bind mounts, propagation, needed before pivot. | §3, §4 |
| [`proc(5)`](https://man7.org/linux/man-pages/man5/proc.5.html) | What the masked/read-only `/proc` paths expose. | §4 |

## Kernel source (primary)

- [`fs/open.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/open.c),
  `SYSCALL_DEFINE1(chroot, ...)` — sets `fs->root` only.
- [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c),
  `SYSCALL_DEFINE2(pivot_root, ...)` — the requirement checks and the root-mount
  swap.

## Container runtimes and specs (for connections)

- OCI Runtime Specification:
  [config.md — Root](https://github.com/opencontainers/runtime-spec/blob/main/config.md#root),
  [config-linux.md — Masked/Readonly Paths](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#masked-paths),
  [Default Devices and Filesystems](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#default-devices).
- runc source: [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go)
  — `setupRootfs`, `pivotRoot`, `mountToRootfs`, `maskPath`, `readonlyPath`.
- OCI Image Specification: [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  — how layers become a rootfs (Chapter 08).
- Docker: [`docker run --read-only`](https://docs.docker.com/reference/cli/docker/container/run/);
  Kubernetes: [Security Context](https://kubernetes.io/docs/tasks/configure-pod-container/security-context/).

## Tools for building a rootfs

- [`debootstrap(8)`](https://manpages.debian.org/stable/debootstrap/debootstrap.8.en.html)
  — a Debian/Ubuntu userland from packages.
- [BusyBox](https://www.busybox.net/about.html) — the single-binary userland used
  in the labs.
- Filesystem Hierarchy Standard: [FHS 3.0](https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html).

## Secondary sources (selected)

- NCC Group, [Understanding and Hardening Linux Containers](https://www.nccgroup.com/us/research-blog/understanding-and-hardening-linux-containers/)
  — the chroot-vs-pivot_root distinction and masked-path rationale in a security
  context.
- Liz Rice, ["Containers From Scratch"](https://www.youtube.com/watch?v=8fi7uSYlOdc)
  (GOTO 2018) — live-codes the rootfs + pivot_root steps in Go (preview of
  Chapter 09).
