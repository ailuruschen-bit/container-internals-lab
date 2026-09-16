# 3. pivot_root

## The problem with chroot, restated

`chroot()` redirects path lookups to a new root, but the **old root filesystem is
still mounted** underneath, and a privileged process can reach it (section 2).
For a container, we want the host's filesystem to become **completely
unreachable**: not merely hidden by a redirected root, but detached from the
mount tree the container can see.

`pivot_root()` does this. It is the operation container runtimes actually use.

## The mechanism

```c
#include <sys/syscall.h>
long syscall(SYS_pivot_root, const char *new_root, const char *put_old);
```

`pivot_root(new_root, put_old)`:

1. makes `new_root` the **root mount** of the calling process's mount namespace;
2. moves the **previous root mount** so that it is now located at `put_old`
   (which must be a directory at or under `new_root`).

After the call, `/` is the former `new_root`, and the old host root is accessible
at `put_old` — until the runtime unmounts it. Once `put_old` is unmounted
(usually lazily), **there is no path from the container to the host filesystem
at all**, because it is no longer part of the mount tree the namespace holds.

```text
before pivot_root                     after pivot_root(/newroot, /newroot/oldroot)
mount tree of the namespace           mount tree of the namespace
/            host root                /            = former /newroot
├── newroot  the container rootfs     └── oldroot  = former host root  (then unmounted)
│   └── oldroot (empty dir)
└── ...                               (nothing else reachable)
```

## The requirements

`pivot_root()` is picky, by design. All of these must hold, or it fails
(commonly with `EINVAL`):

1. **`new_root` and `put_old` must be directories**, and `put_old` must be at or
   underneath `new_root`.
2. **`new_root` must be a mount point.** A plain directory is not enough; the
   container rootfs must itself be a mount. Runtimes ensure this by
   **bind-mounting the rootfs onto itself** first (Chapter 02 §3 mentioned this
   technique): `mount("/newroot", "/newroot", NULL, MS_BIND|MS_REC, NULL)`.
3. **`new_root` must not be the current root**, and neither may be on a
   **shared** mount that would propagate the change. This is why runtimes make
   the tree private/slave first (`mount(NULL, "/", NULL, MS_REC|MS_PRIVATE/SLAVE,
   NULL)`), which is exactly the propagation step from Chapter 03 §4.
4. The caller needs **`CAP_SYS_ADMIN`** in the mount namespace's user namespace.
5. It must be called in a **mount namespace** other than the initial one for
   container use (you can call it in the initial namespace, but you would pivot
   the host, which is not what you want).

## The canonical sequence

The steps a runtime performs, each tying back to earlier chapters:

```text
unshare(CLONE_NEWNS)                             new mount namespace   (Ch. 03 §4)
mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL)  stop propagation      (Ch. 03 §4, Ch. 02 §4)
mount(rootfs, rootfs, NULL, MS_BIND|MS_REC, 0)   make rootfs a mount point (Ch. 02 §3)
mkdir(rootfs + "/oldroot")                        the put_old directory
chdir(rootfs)
pivot_root(".", "oldroot")                        swap roots
chdir("/")
umount2("/oldroot", MNT_DETACH)                   detach the host root  (Ch. 02 §2, lazy unmount)
rmdir("/oldroot")
```

The use of `.` and a relative `put_old` after `chdir(rootfs)` is the common,
robust idiom (it avoids path-resolution surprises). After the final
`umount2(..., MNT_DETACH)`, the container's mount namespace contains only the
container's mounts.

## pivot_root vs chroot for containers

| | `chroot` | `pivot_root` |
|---|---|---|
| Changes | the process's root **directory** | the mount namespace's root **mount** |
| Old root afterwards | still mounted underneath, reachable by privileged escape | can be **detached**, then unreachable |
| Needs a mount namespace | no | in practice yes |
| Needs the target to be a mount | no | yes |
| Used by real runtimes | rarely, as a fallback | **yes**, the default |

runc uses `pivot_root` by default and falls back to a `MS_MOVE` + `chroot`
approach only in environments where `pivot_root` is not possible (for example
certain `initramfs`/`rootfs` situations).

## Why this matters for containers

- This is the step that turns "a mount namespace with a copy of the host tree"
  (Chapter 03 §4) into "a container with its own, isolated root". It is the
  culmination of Chapters 02 and 03.
- The requirements explain several runtime behaviors you can observe: the
  self-bind-mount of the rootfs, the propagation change to `rslave`/`rprivate`,
  and the lazy unmount of the old root.
- If you ever see `pivot_root: Invalid argument` when building a container by
  hand, it is almost always requirement 2 (rootfs not a mount point) or 3
  (shared propagation).

## Evidence

Lab: [`lab-03-pivot-root`](../../labs/07-rootfs-chroot-pivot-root/lab-03-pivot-root/)

## Further Reading

- [`pivot_root(2)`](https://man7.org/linux/man-pages/man2/pivot_root.2.html) —
  the requirements, the `EINVAL` conditions, and the notes on shared mounts and
  on the `.`/relative idiom. The authoritative source for this section.
- [`pivot_root(8)`](https://man7.org/linux/man-pages/man8/pivot_root.8.html) —
  the command-line tool used in the lab.
- [`mount_namespaces(7)`](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
  — why propagation must be changed before pivoting.
- runc source: [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go),
  functions `pivotRoot()` and `prepareRoot()` — the exact sequence above in
  production Go (Chapter 11 reads this in depth).
- Kernel source: [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c),
  `SYSCALL_DEFINE2(pivot_root, ...)` — the requirement checks in C.
