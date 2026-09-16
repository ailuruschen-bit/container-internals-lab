# 2. chroot and Why It Is Not a Boundary

## The mechanism

`chroot()` changes the calling process's **root directory** (the `fs->root`
field from Chapter 01 §1, the starting point of absolute path resolution from
Chapter 02 §1):

```c
#include <unistd.h>
int chroot(const char *path);
```

After `chroot("/tmp/rootfs")`, the process (and its children) resolve `/` as
`/tmp/rootfs`. `open("/etc/passwd")` opens `/tmp/rootfs/etc/passwd`. It requires
`CAP_SYS_CHROOT`.

`chroot()` is old (1979) and simple. It changes only the root directory; it does
**not** change the current working directory. A well-behaved caller does
`chroot(path)` then `chdir("/")`. Forgetting the `chdir` leaves the working
directory outside the new root, which is one route to the escapes below.

## Why it is not a security boundary

`chroot()` was designed for **build isolation and testing**, not for confining
hostile code. Several documented techniques let a process with enough privilege
escape it:

### 1. The classic `chdir` / `..` escape

If a process keeps a file descriptor to a directory **outside** the new root, or
did not `chdir("/")` into it, it can `fchdir()` back out and then `chroot(".")`
repeatedly, walking up with `..` until it reaches the real root. The kernel does
not forget the old root for `..` unless the working directory is inside the new
root, and even then a leaked directory fd defeats it. Minimal reproduction:

```c
mkdir("escape", 0755);
chroot("escape");         /* new root is .../escape, but cwd is unchanged */
for (int i = 0; i < 1000; i++) chdir("..");   /* climb above the new root */
chroot(".");              /* set root to the real filesystem root */
execl("/bin/sh", "sh", NULL);
```

### 2. It only affects the filesystem root

`chroot()` changes **one** field. It does not create a mount namespace, PID
namespace, network namespace, or new credentials. A chrooted process:

- still sees all host processes and can signal them (no PID namespace);
- still shares the host's mount table, so `/proc/<pid>/root` from outside reveals
  it, and mounts still propagate;
- still has its original UIDs and capabilities: a chrooted **root** process can
  `mknod` a device node for the host disk and read it, `mount` over paths, or
  load a module;
- keeps any open file descriptors to outside objects (Chapter 01 §5).

### 3. Root can simply undo it

Any process that retains `CAP_SYS_CHROOT` and `CAP_MKNOD`/`CAP_SYS_ADMIN` can
create a path to the host and re-`chroot`, or create a device node and access the
host filesystem directly.

The lesson is not "chroot is useless" but "chroot changes the root directory and
**only** the root directory". Isolation requires the *other* mechanisms from
Chapters 03–06 as well.

## Why containers do not rely on chroot alone

A container needs the root change to be combined with:

- a **mount namespace** (Chapter 03 §4), so the change is private and the host's
  mounts can be detached, not merely hidden;
- **dropped capabilities** and **seccomp** (Chapters 05–06), so a container root
  cannot re-mount or `mknod` its way out;
- ideally a **user namespace** (Chapter 03 §7), so container root is not host
  root.

Even with all that, runtimes prefer **`pivot_root()`** over `chroot()` (section
3), because `pivot_root` can make the old root **completely unreachable** by
detaching its mount, while `chroot` only redirects path lookups and leaves the
old tree mounted underneath.

## Why this matters for containers

- "A container is just chroot" is a common and wrong simplification. This section
  is the precise reason it is wrong.
- Understanding chroot's escapes is understanding why each *other* layer exists:
  each escape corresponds to a mechanism a real runtime adds.
- `chroot` is still useful and used: for entering a rootfs to debug it, in
  installers and build systems, and as one step some runtimes take before or
  instead of `pivot_root` in constrained environments.

## Evidence

Lab: [`lab-02-chroot-and-escape`](../../labs/07-rootfs-chroot-pivot-root/lab-02-chroot-and-escape/)

## Further Reading

- [`chroot(2)`](https://man7.org/linux/man-pages/man2/chroot.2.html) — the
  syscall and, in NOTES, the explicit statement that it is not intended to defend
  against privileged escape, plus the `..`/cwd behavior.
- [`chroot(1)`](https://man7.org/linux/man-pages/man1/chroot.1.html) — the
  command used in the labs.
- [`path_resolution(7)`](https://man7.org/linux/man-pages/man7/path_resolution.7.html)
  — how the root directory bounds `..`, which the escape exploits.
- Kernel source: [`fs/open.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/open.c),
  `SYSCALL_DEFINE1(chroot, ...)` — it sets `fs->root` and nothing else, which is
  the whole point of this section.
