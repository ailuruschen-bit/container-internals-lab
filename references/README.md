# References

Annotated primary sources for each chapter. Every entry explains **why** it is
worth reading, not only where to find it.

| Chapter | File |
|---|---|
| 01 — Linux process fundamentals | [01-linux-process.md](01-linux-process.md) |
| 02 — Linux filesystem | [02-linux-filesystem.md](02-linux-filesystem.md) |
| 03 — Namespaces | [03-namespaces.md](03-namespaces.md) |
| 04 — cgroups v2 | [04-cgroups.md](04-cgroups.md) |
| 05 — Capabilities | [05-capabilities.md](05-capabilities.md) |
| 06 — seccomp | [06-seccomp.md](06-seccomp.md) |
| 07 — rootfs, chroot, pivot_root | [07-rootfs-chroot-pivot-root.md](07-rootfs-chroot-pivot-root.md) |
| 08 — OverlayFS | [08-overlayfs.md](08-overlayfs.md) |
| 09 — Build a container | [09-build-a-container.md](09-build-a-container.md) |
| 10 — OCI specifications | [10-oci-runtime-spec.md](10-oci-runtime-spec.md) |
| 11 — runc internals | [11-runc-internals.md](11-runc-internals.md) |
| 12 — containerd internals | [12-containerd-internals.md](12-containerd-internals.md) |

Conventions:

- Prefer Linux man-pages, kernel documentation, OCI specifications, and the
  upstream runc / containerd / Moby repositories.
- Kernel source links use a pinned version on Elixir (bootlin). Upstream
  runtime source links will be pinned to a release tag or commit.
- When a detail is version-dependent, the version is stated next to it.
