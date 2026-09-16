# 4. Chapter Summary: Layers, Shared and Private

## The model you should now have

A container's rootfs is an **OverlayFS mount**: the image's layers are read-only
`lowerdir`s (shared by every container using them), and one writable `upperdir`
holds this container's changes. Reads come from the highest layer with the file;
writes go to `upperdir`, copying a lower file up on first modification
(**copy-up**); deletions of lower files are recorded as **whiteouts**.

```text
merged (the container rootfs, used for pivot_root in Ch. 07)
   ▲
   │ top wins; writes land in upper; copy-up on first modify; whiteouts hide deletions
upperdir   (writable, per container, ephemeral)   ← docker diff / docker commit
lowerdir   (image layers, read-only, content-addressed, SHARED)
workdir    (OverlayFS's private scratch; never touch)
```

## Key facts

| Fact | Section |
|---|---|
| Union filesystem: stacked layers presented as one tree, top wins | 1 |
| Copy-on-write: first modify of a lower file copies the whole file up | 1, 2 |
| Whiteout = 0/0 char device in upper; opaque dir = `trusted.overlay.opaque` xattr | 2 |
| `lowerdir` leftmost is highest; `upperdir`+`workdir` share one filesystem | 2 |
| An image is layer tarballs (diffs) with `.wh.` deletions, content-addressed | 3 |
| Runtimes unpack layers, overlay-mount them, and use `merged` as the rootfs | 3 |
| Image `.wh.` whiteouts differ from OverlayFS 0/0 whiteouts; runtimes translate | 3 |
| The writable layer is ephemeral; persistent data needs a volume | 3 |

## The sentence to remember

> An image is stored once as shared, read-only layers; each container adds a
> private writable layer, and OverlayFS presents the stack as one filesystem, so
> containers start fast and use little disk.

## Self-check questions

1. What do `lowerdir`, `upperdir`, `workdir`, and `merged` each hold? Which two
   must be on the same filesystem? (§2)
2. Explain copy-up and why the first write to a large image file is slow. (§1, §2)
3. What is a whiteout on disk, and why can OverlayFS not simply delete the lower
   file? (§2)
4. How does a container image's deletion representation differ from OverlayFS's,
   and who translates between them? (§3)
5. A container `rm`s a 1 GB file from its base image. Does disk usage drop? Where
   is the data? (§2, §3)
6. Why can 100 containers from one image start almost instantly and use little
   extra disk? (§1, §3)
7. Where does data written to a container's filesystem go, and why is it lost when
   the container is removed? (§3)

## Next: Chapter 09 — Build a container manually

Every Linux primitive is now covered: processes, filesystem, namespaces,
cgroups, capabilities, seccomp, rootfs/pivot_root, and OverlayFS. Chapter 09
assembles them into a small container runtime in Go, one isolation step at a
time, always asking "what changed, and what did **not**", and arriving at a
process that is, in every meaningful way, a container, without Docker.
