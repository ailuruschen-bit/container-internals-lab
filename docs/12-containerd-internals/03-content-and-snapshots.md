# 3. Content, Images, and Snapshotters

This section follows an image from `pull` to a mountable rootfs, connecting
Chapter 10 §4 (the image graph) and Chapter 08 (OverlayFS) to containerd's
services.

## The content store: immutable blobs by digest

The **content store** (Content service) holds every object of an image as an
immutable blob keyed by its digest (Chapter 10 §4): the manifest, the image
config, and each compressed layer. On disk it is under
`/var/lib/containerd/io.containerd.content.v1.content/blobs/sha256/<digest>`.

Properties, all from content addressing (Chapter 10 §4):

- **Dedup:** a layer shared by two images is stored once.
- **Integrity:** the blob's digest verifies its bytes.
- **Immutable:** blobs are never modified, only added and garbage-collected when
  nothing references them.

`pull` = fetch the manifest (resolve the tag → digest), then fetch the config and
any missing layer blobs, all into the content store (the Distribution API,
Chapter 10 §4).

## From content to a filesystem: snapshotters

Layer blobs are compressed tarballs of diffs (Chapter 08 §3); they are not
directly mountable. The **Snapshots service** turns them into a usable rootfs via
a **snapshotter** plugin. The default is `overlayfs` (Chapter 08).

The unpack + prepare flow:

```text
for each layer, lowest → highest:
    Diff service applies the layer tarball onto the previous snapshot,
    producing a new COMMITTED snapshot (read-only)               (Ch. 08 §3)
        - .wh. image whiteouts are translated to overlay whiteouts (Ch. 08 §3)
to run a container:
    Snapshots.Prepare(key, parent=top committed snapshot)
        creates an ACTIVE snapshot = a writable upper layer       (Ch. 08 §2)
        and returns the MOUNTS to assemble the rootfs:
            overlay lowerdir=<committed snapshots> upperdir=<active> workdir=<...>
```

The snapshotter returns **mount descriptors**, not a mounted path; the shim/runc
applies them when building the rootfs. This is exactly the overlay mount you built
in Chapter 08 Lab 01, with the committed snapshots as `lowerdir` and the active
snapshot as `upperdir`.

### Snapshot states

| State | Meaning |
|---|---|
| **committed** | read-only, immutable; an unpacked image layer, shareable |
| **active** | writable; a container's upper layer (or an in-progress unpack) |
| **view** | a read-only mount of a committed snapshot (e.g. to inspect) |

Committed snapshots are shared by every container built on them; each container
gets its own active snapshot (Chapter 08 §1's "shared read-only, private
writable").

### Snapshotter choices

`overlayfs` is default. Others (Chapter 08 §3): `native` (full copies, no
overlay), `btrfs`/`zfs` (filesystem-native snapshots), and **lazy-pulling**
snapshotters (`stargz`, `nydus`) that mount a layer and fetch its contents on
demand, so a container can start before the whole image is downloaded. All
implement the same Snapshotter interface, so containerd and images are unchanged.

## Images service: names to digests

The **Images service** stores the mapping from an image **name/tag**
(`docker.io/library/nginx:1.27`) to the manifest **digest** in the content store.
This is the mutable pointer; the content it points to is immutable. Retagging or
pulling a new `:latest` just repoints the name at a new digest.

## Putting it together

```text
ctr image pull docker.io/library/nginx:1.27
   Distribution API → Content store: manifest, config, layer blobs (by digest)
   Diff + Snapshots: unpack layers → committed snapshots            (Ch. 08 §3)
   Images: record  nginx:1.27 → manifest digest
ctr run ... nginx:1.27 web ...
   Containers: build the OCI spec from the image config + options    (Ch. 10 §2,§4)
   Snapshots.Prepare: active snapshot + overlay mounts for the rootfs (Ch. 08 §2)
   Tasks + shim + runc: create the container from the bundle          (Ch. 11)
```

## Why this matters

- This is the concrete realization of Chapter 08 §3 ("runtimes unpack layers and
  overlay-mount them") and Chapter 10 §4 ("an image becomes a bundle"), named and
  located in containerd.
- It explains observable facts: where image bytes live, why pulls dedup, why each
  container has a private writable snapshot, and how lazy snapshotters start
  containers before the full download.

## Evidence

Lab: [`lab-02-content-and-snapshots`](../../labs/12-containerd-internals/lab-02-content-and-snapshots/)

## Further Reading

- containerd [content flow](https://github.com/containerd/containerd/blob/main/docs/content-flow.md)
  and [snapshotters](https://github.com/containerd/containerd/blob/main/docs/snapshotters/README.md).
- containerd [`core/snapshots`](https://github.com/containerd/containerd/tree/main/core/snapshots)
  (the Snapshotter interface) and [`core/content`](https://github.com/containerd/containerd/tree/main/core/content).
  (In containerd 1.7 these are under `snapshots/` and `content/`; paths moved to
  `core/` in 2.0 — check your version.)
- Chapters 08 §2–§3 (OverlayFS) and 10 §4 (the image graph).
