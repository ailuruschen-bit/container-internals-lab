# Chapter 08 — OverlayFS

## Why this chapter exists

Chapter 07 treated the container's root filesystem as a ready-made directory. But
where does that directory come from, and why can a hundred containers from the
same image start in milliseconds without copying gigabytes each?

The answer is a **union filesystem**. A container image is a stack of
**read-only layers**; when a container starts, the runtime adds one **writable
layer** on top and presents the union as a single directory tree, the rootfs.
Writes go to the top layer; the shared read-only layers are never modified, so
they can be shared by every container that uses them, and copied on first write.

Linux implements this with **OverlayFS**, a filesystem you mount with `mount -t
overlay`. This chapter builds an overlay by hand, watches exactly where changes
land, and only then connects it to container image layers.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [The problem and the union idea](01-union-idea.md) | Why copying images is wasteful; layers and copy-on-write. |
| 2 | [OverlayFS by hand](02-overlayfs-by-hand.md) | `lowerdir`, `upperdir`, `workdir`, `merged`; copy-up; whiteouts. |
| 3 | [Image layers and snapshotters](03-image-layers.md) | How images map to overlay layers; the writable layer; graph/snapshot drivers. |
| 4 | [Chapter summary](04-summary.md) | What changed, what did not. |

Labs: [`labs/08-overlayfs/`](../../labs/08-overlayfs/).
References: [`references/08-overlayfs.md`](../../references/08-overlayfs.md).

## Prerequisites

- [Chapter 02](../02-linux-filesystem/): VFS, inodes vs names (§1), mounts (§2).
- [Chapter 07](../07-rootfs-chroot-pivot-root/): what a rootfs is (§1).

## Environment

Linux VM with `sudo`, a kernel with OverlayFS (any modern kernel), and
`util-linux`. Overlay can be mounted inside a user namespace on Linux 5.11+, so
some labs also work rootless. Optional: `docker` or `podman` for the layer
inspection lab.
