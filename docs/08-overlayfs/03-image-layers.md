# 3. Image Layers and Snapshotters

This section connects the hand-built overlay to real container images: how an
image's layers become `lowerdir`, where the writable layer lives, and the small
but important differences between the **image format** and the **filesystem**.

## An image is a stack of layer tarballs plus metadata

An OCI/Docker image is not a filesystem; it is a set of files described by JSON:

```text
image
├── manifest         lists the config and the ordered layers (by digest)
├── config           the runtime config: env, entrypoint, and the rootfs diff_ids
└── layers[]         each layer is a gzipped tar archive of a filesystem DIFF
```

Each **layer** is a **tar archive of the changes** relative to the layer below:
files added or modified are present; files deleted are represented by a special
entry named `.wh.<filename>` (a "whiteout" in the *image* format). Layers are
**content-addressed**: identified by the SHA-256 digest of their content, so two
images with an identical layer refer to the same digest and store it once.

## From image layers to an overlay mount

When a runtime prepares a container, it:

1. **pulls** the layers (if not cached) into a local **content store**,
   keyed by digest (Chapter 12 covers the content store);
2. **unpacks** each layer tarball into a directory (a "snapshot"), applying
   `.wh.` entries as it extracts, so each unpacked layer is a normal directory
   tree;
3. **mounts an overlay** whose `lowerdir` is the ordered list of unpacked layer
   directories, with a fresh `upperdir` and `workdir` for this container;
4. uses the resulting `merged` directory as the container's **rootfs** for
   `pivot_root` (Chapter 07).

```text
image layers (tarballs, by digest)      unpacked snapshots            overlay for one container
sha256:aaa (base)          ──unpack──►  .../snapshots/1/fs   ─┐
sha256:bbb (apt install)   ──unpack──►  .../snapshots/2/fs   ─┼─ lowerdir (read-only, shared)
sha256:ccc (COPY app)      ──unpack──►  .../snapshots/3/fs   ─┘
                                        .../containers/<id>/upper  ── upperdir (writable, private)
                                        .../containers/<id>/work   ── workdir
                                        mount -t overlay ...  ──►   .../merged  = container rootfs
```

Every container from the same image shares snapshots 1–3 read-only and gets its
own `upper`. This is the disk-and-speed win from section 1, realized.

## Two kinds of whiteout: image vs OverlayFS

A subtle but important detail: the **image format** and **OverlayFS** represent
deletions differently.

| | Image layer (tar) | OverlayFS (on disk) |
|---|---|---|
| Deleted file | a regular file named `.wh.<name>` | a character device 0/0 named `<name>` |
| Opaque directory | a file `.wh..wh..opq` in the directory | xattr `trusted.overlay.opaque=y` |

When a runtime unpacks an image layer, it **translates** the image's `.wh.`
entries into the filesystem's native whiteouts (or applies them directly during
extraction). This is why you do not see `.wh.` files inside a running container's
overlay, only 0/0 char devices, and why `docker save` of a running container's
diff converts them back. Keeping these two representations straight avoids a lot
of confusion when inspecting images vs containers.

## Snapshotters and graph drivers

The component that manages "unpack layers and produce a mountable rootfs" has
different names across the stack:

- In **Docker/moby** it is a **storage driver** (historically "graph driver"):
  `overlay2` is the default; older systems used `aufs`, `devicemapper`, `btrfs`,
  `zfs`. `overlay2` uses OverlayFS exactly as this chapter describes.
- In **containerd** it is a **snapshotter**: `overlayfs` is the default;
  alternatives include `native` (full copies), `btrfs`, `zfs`, and lazy-pulling
  snapshotters like `stargz`/`nydus` that mount layers before they are fully
  downloaded. Chapter 12 covers snapshotters in depth.

They all solve the same problem this chapter poses; OverlayFS is the common
default because it is in the mainline kernel and needs no special storage setup.

## The writable layer is ephemeral

Because the container's changes live only in `upperdir`, and the runtime deletes
`upperdir` when the container is removed:

- data written to the container filesystem is **lost** when the container is
  removed; persistent data must go in a **volume** (a bind mount, Chapter 02 §3)
  or a named volume, which bypasses the overlay;
- `docker commit` turns a container's `upperdir` diff into a **new image layer**;
- image size and container disk usage are different things: image size is the sum
  of (shared) layer sizes; a container adds only its `upperdir`.

## Why this matters for containers

- This is the bridge from "image" to the "rootfs directory" Chapter 07 assumed
  and Chapter 09 will use. Pulling, unpacking, and overlay-mounting is what turns
  a registry artifact into a running container's `/`.
- The image-vs-OverlayFS whiteout distinction explains inspection surprises and
  is exactly the kind of detail Chapter 12 (containerd) makes concrete.
- Volumes vs the writable layer is a daily operational decision that this
  mechanism explains.

## Evidence

Lab: [`lab-03-image-layers`](../../labs/08-overlayfs/lab-03-image-layers/)

## Further Reading

- OCI Image Specification: [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  — the tar layer format, `.wh.` whiteouts, `.wh..wh..opq` opaque markers, and
  `diff_id`s. The authoritative source for the image side.
- Docker: [About storage drivers](https://docs.docker.com/engine/storage/drivers/)
  and [overlayfs storage driver](https://docs.docker.com/engine/storage/drivers/overlayfs-driver/)
  — how `overlay2` lays out `diff/`, `merged/`, `work/`, and `link` directories
  under `/var/lib/docker/overlay2`.
- containerd: [Snapshotters](https://github.com/containerd/containerd/blob/main/docs/snapshotters/README.md)
  — the snapshotter interface and the available implementations (Chapter 12).
- Kernel docs: [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html),
  the whiteout section, to compare with the image-format whiteouts above.
