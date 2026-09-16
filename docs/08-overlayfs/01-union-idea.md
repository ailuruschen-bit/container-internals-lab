# 1. The Problem and the Union Idea

## The problem: images are large and shared

Suppose 50 containers run the same 200 MB image, each writing a few megabytes.
Naive approaches are wasteful:

- **Copy the whole rootfs per container.** 50 × 200 MB = 10 GB of mostly
  identical data, and each start copies 200 MB (slow).
- **Share one rootfs read-write.** Containers would corrupt each other's files.
- **Share one rootfs read-only.** Containers could not write anything (no logs,
  no temp files, no package installs).

What we want: the 200 MB is stored **once** and shared read-only; each container
gets a small **private** area for its own changes; and a container sees a single,
normal-looking filesystem that combines both.

## The union idea

A **union filesystem** presents several directory trees stacked on top of each
other as **one** merged tree:

```text
     merged view (what the container sees as /)
     ┌─────────────────────────────────────┐
     │  a.txt  b.txt  c.txt  new.txt        │
     └─────────────────────────────────────┘
              ▲ top wins on conflicts
   upper (writable)   │  c.txt (modified)  new.txt
   ───────────────────┼──────────────────────────
   lower (read-only)  │  a.txt  b.txt  c.txt (original)
```

Rules of the union:

- A file present in an upper layer **shadows** the same-named file in a lower
  layer (top wins).
- Reads come from the highest layer that has the file.
- **Writes go only to the writable (upper) layer.** The read-only layers are
  never modified.

This directly solves the problem: the read-only layers are the shared image; the
upper layer is the container's private changes.

## Copy-on-write

What happens when a container **modifies** a file that exists only in a
read-only lower layer? The union cannot write to the lower layer. Instead it
performs **copy-up**: the whole file is copied from the lower layer into the
upper layer, and the modification is applied to the copy. From then on, the
upper copy shadows the lower original.

Consequences you can observe (and will, in the lab):

- The **first write** to a large lower-layer file is slow (it copies the whole
  file), even if you change one byte. Later writes are fast.
- Copy-up happens on the first operation that needs a writable version:
  `open(O_WRONLY)`, `truncate`, changing permissions or ownership, etc.
- The lower file is untouched, so another container sharing the same lower layer
  still sees the original.

## Deleting and the whiteout problem

How do you **delete** a file that exists in a read-only lower layer? You cannot
remove it from the lower layer. The union records the deletion in the upper layer
with a special marker called a **whiteout**. When the merged view is read, a
whiteout hides the lower-layer file, so it appears gone. Similarly, replacing a
lower directory with an **opaque** marker hides all of the lower directory's
contents.

The container sees a normal `rm`; underneath, the lower file still exists but is
masked. Section 2 shows exactly what a whiteout looks like on disk.

## Layers stack

Images have **many** lower layers, not one. Each `RUN`, `COPY`, or `ADD` in a
Dockerfile typically produces one layer. They stack lowest-to-highest, each
shadowing the ones below, with the container's writable layer on top:

```text
container writable layer   (upper)   ← the running container's changes
image layer 5              (lower)   ← COPY app.jar
image layer 4              (lower)   ← RUN apt-get install ...
...
image layer 1              (lower)   ← the base image (e.g. debian)
```

Because the lower layers are content-addressed and read-only, two images that
share a base layer store it **once** on the host, and every container from those
images shares it.

## Why this matters for containers

- This is why `docker pull` of a second image sharing a base is fast and small,
  why containers start quickly, and why disk usage is far less than
  images × containers.
- It is why the container's writable layer is ephemeral: deleting the container
  discards the upper layer, and the image layers remain. Data you must keep goes
  in a volume (a bind mount, Chapter 02 §3), not the writable layer.
- Copy-up cost explains performance surprises: rewriting large files that came
  from the image is slower than writing new files.

## Evidence

Lab: [`lab-01-overlay-basics`](../../labs/08-overlayfs/lab-01-overlay-basics/)

## Further Reading

- Kernel docs: [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html),
  sections "Overlay objects" and "Non-directories" (copy-up) and "Whiteouts and
  opaque directories". The authoritative source; this chapter is a guided reading
  of it.
- Docker: [About storage drivers](https://docs.docker.com/engine/storage/drivers/)
  and [overlayfs driver](https://docs.docker.com/engine/storage/drivers/overlayfs-driver/)
  — how images map to overlay layers and copy-on-write in practice.
- OCI Image Specification: [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  — how layers (including whiteout files, `.wh.` entries) are represented in an
  image, which differs slightly from OverlayFS's on-disk whiteouts (section 3).
