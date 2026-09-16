# References — Chapter 08: OverlayFS

Version note: kernel docs are current; image-format details follow the OCI Image
Specification v1.

## Kernel documentation and man-pages (primary)

| Document | Why read it | Used in |
|---|---|---|
| [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html) | The definitive reference: layers, copy-up, whiteouts, opaque dirs, workdir, metacopy, and mounting rules. This chapter is a guided reading of it. | §1–3 |
| [`mount(8)`](https://man7.org/linux/man-pages/man8/mount.8.html) | The `overlay` type options `lowerdir`/`upperdir`/`workdir`. | §2 |
| [`mknod(1)`](https://man7.org/linux/man-pages/man1/mknod.1.html) | How a 0/0 char-device whiteout is formed. | §2 |
| [`getfattr(1)`](https://man7.org/linux/man-pages/man1/getfattr.1.html) | Reading `trusted.overlay.opaque`. | §2 |

## Specifications (primary for §3)

- OCI Image Specification: [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  — the tar layer format, `.wh.<name>` whiteouts, `.wh..wh..opq` opaque markers,
  and `diff_id`s.
- OCI Image Specification: [manifest.md](https://github.com/opencontainers/image-spec/blob/main/manifest.md)
  and [config.md](https://github.com/opencontainers/image-spec/blob/main/config.md)
  — how a manifest lists layers and the config records `rootfs.diff_ids`.

## Container runtimes (primary for §3)

- Docker: [About storage drivers](https://docs.docker.com/engine/storage/drivers/)
  and [overlayfs storage driver](https://docs.docker.com/engine/storage/drivers/overlayfs-driver/)
  — the `/var/lib/docker/overlay2` layout and copy-on-write behavior.
- containerd: [Snapshotters](https://github.com/containerd/containerd/blob/main/docs/snapshotters/README.md)
  — the snapshotter interface and implementations (`overlayfs`, `native`,
  `stargz`, `nydus`); Chapter 12.
- moby graphdriver source: [`daemon/graphdriver/overlay2`](https://github.com/moby/moby/tree/master/daemon/graphdriver/overlay2)
  — the `overlay2` driver in code.

## Secondary sources (selected)

- LWN overlayfs coverage, e.g. ["Unionmount and overlayfs"](https://lwn.net/Articles/396020/)
  and later articles — design history and corner cases.
- Docker: [Volumes](https://docs.docker.com/engine/storage/volumes/) — why
  persistent data belongs outside the writable overlay layer.
