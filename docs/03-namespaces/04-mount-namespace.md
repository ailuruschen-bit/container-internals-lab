# 4. Mount Namespace

## 1. The global resource before isolation

Chapter 02 described the **mount tree**: the structure path resolution walks to
turn a path into a file. In every Chapter 02 lab, a mount was immediately
visible to every process on the machine, which is why the labs used scratch
directories and careful cleanup. Without mount namespaces there is one mount
tree, so:

- any mount affects all processes;
- one process cannot have a different `/proc`, `/tmp`, or `/` layout than
  another (apart from `chroot`, Chapter 07);
- a program that needs a private mount (for example a build sandbox) must
  modify the host.

## 2. What the namespace isolates

A mount namespace owns **its own mount tree**, the set of `struct mount`
objects reachable from its root mount. `/proc/<pid>/mountinfo` shows the mount
tree of the namespace that `<pid>` belongs to, which is why Chapter 02 §2
explained that the file is per process.

The mount namespace was the first namespace added to Linux (2002), before
anyone planned other types, which is why its flag has the generic name
`CLONE_NEWNS`.

## 3. What changes from the process's perspective

When a mount namespace is created, the kernel **copies the creator's mount
tree**: every mount is duplicated into a new `struct mount` in the new
namespace. The filesystems (super blocks) and their files are **not** copied;
both trees refer to the same filesystem instances.

```text
host mount namespace                     new mount namespace (right after creation)
/        ext4 vda1   ───── same super block ─────   /        ext4 vda1   (copy of mount)
/proc    proc        ───── same super block ─────   /proc    proc        (copy of mount)
/tmp     tmpfs       ───── same super block ─────   /tmp     tmpfs       (copy of mount)
```

Consequences:

| Action in the new namespace | Visible on the host? |
|---|---|
| `mount` or `umount` of something | depends on **propagation** (below) |
| Create, modify, delete a **file** on a copied mount | **yes**: same filesystem, same inodes |
| Change per-mount flags (e.g. remount read-only) | no: the mount object is a copy |

The second row surprises many people: a mount namespace isolates **mounts**,
not **files**. Writing `/tmp/x` in a new mount namespace writes the host's
`/tmp/x`, unless something new has been mounted at `/tmp` inside the namespace.
File-level isolation of a container comes from mounting a *different*
filesystem as its root (Chapters 07–08).

### Propagation across namespaces

Chapter 02 §4 introduced peer groups using bind mounts. Here is where they
matter most. When a mount namespace is created:

- each **shared** mount in the original tree becomes a **peer** of its copy in
  the new namespace;
- **private** mounts are copied as private;
- **slave** mounts are copied as slaves of the same master.

On a systemd host, nearly everything is shared, so a fresh mount namespace is
still **connected in both directions** to the host: a mount made inside appears
outside and vice versa. To get real isolation, the process in the new namespace
must change propagation immediately:

```c
unshare(CLONE_NEWNS);
mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);   // or MS_SLAVE to keep receiving host mounts
```

The `unshare` command does the `MS_REC | MS_PRIVATE` step **by default**
(`--propagation private`). This default hides the underlying behavior; the lab
uses `--propagation unchanged` to show it.

### Less-privileged mount namespaces

If a mount namespace is created together with a **new user namespace** (section
7), the kernel treats the copy as *less privileged*. It then:

- turns shared mounts into slave mounts, so the new namespace cannot propagate
  mounts back to the more privileged one;
- **locks** the copied mounts together, so that `umount` or changing flags such
  as `ro`, `nosuid`, `nodev`, `noexec` cannot reveal what is underneath or weaken
  restrictions set by the host.

This rule is what makes unprivileged mount namespaces (rootless containers)
safe.

## 4. Kernel API

- `clone(CLONE_NEWNS)` and `unshare(CLONE_NEWNS)` create a new mount namespace
  with a copy of the caller's tree (`copy_mnt_ns()` in `fs/namespace.c`).
  Unlike PID namespaces, `unshare()` **does** move the caller.
- `setns(fd, CLONE_NEWNS)` joins an existing one. Restrictions matter for
  runtime implementations:
  - the caller needs `CAP_SYS_CHROOT` and `CAP_SYS_ADMIN`;
  - the caller must not share its filesystem attributes (`CLONE_FS`) with
    another task. **Every thread in a multithreaded process shares `CLONE_FS`**,
    so a multithreaded program cannot `setns()` into a mount namespace. The Go
    runtime is always multithreaded, which is one of the main reasons runc
    enters namespaces in C code that runs before the Go runtime starts
    (Chapter 11).
- After joining, the caller's root and working directory are set to the root of
  the new namespace's tree.

## 5. Shell experiment

```bash
sudo unshare --mount bash
mount -t tmpfs private-tmp /mnt
findmnt /mnt                     # visible here
# in another terminal: findmnt /mnt → nothing
```

## 6. How container runtimes use it

Every container gets its own mount namespace. Inside it, a runtime such as runc:

1. makes the whole tree `rslave` (or `rprivate`), so container mounts cannot
   leak to the host (Chapter 02 §4);
2. bind-mounts the container's root filesystem so it is a mount point;
3. mounts `proc`, `sysfs`, a `tmpfs` for `/dev`, `devpts`, `mqueue`, and the
   configured volumes below the new root (Chapter 02 §5);
4. calls `pivot_root()` to make the new root the namespace's `/`, and lazily
   unmounts the old root, so **host mounts are no longer reachable** at all
   (Chapter 07).

After step 4, the container's mount tree contains only the mounts the runtime
placed there. That is the difference between "a copy of the host's tree" and
"a container filesystem".

`docker exec` joins the container's mount namespace with `setns()`, so the
executed command sees the container's filesystem.

## Evidence

Lab: [`lab-04-mount-namespace`](../../labs/03-namespaces/lab-04-mount-namespace/)

## Further Reading

- [`mount_namespaces(7)`](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
  — the complete rules: copying at creation, shared subtrees across namespaces,
  and "Restrictions on mount namespaces" (locked mounts). The most important
  reference for this section.
- [`setns(2)`](https://man7.org/linux/man-pages/man2/setns.2.html), the
  `CLONE_NEWNS` paragraph — the `CAP_SYS_CHROOT` and `CLONE_FS` restrictions.
- Michael Kerrisk, LWN,
  ["Mount namespaces and shared subtrees"](https://lwn.net/Articles/689856/)
  (2016) — the same propagation experiments as this section's lab, across
  namespaces, with diagrams.
- [`unshare(1)`](https://man7.org/linux/man-pages/man1/unshare.1.html),
  option `--propagation` — documents the default that hides propagation.
- Kernel source: [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c),
  `copy_mnt_ns()` and `copy_tree()` — the copy of the mount tree, including the
  `CL_SLAVE` and lock handling for less-privileged namespaces.
