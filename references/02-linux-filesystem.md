# References — Chapter 02: Linux Filesystem

Version note: kernel documentation links point to the current docs at
docs.kernel.org; kernel source links are pinned to Linux **v6.12**.

## Kernel documentation (primary)

| Document | Why read it | Used in |
|---|---|---|
| [Overview of the Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html) | The official description of `super_block`, `inode`, `dentry`, and `file` and their operation tables. | §1 |
| [Pathname lookup](https://docs.kernel.org/filesystems/path-lookup.html) | Neil Brown's readable walkthrough of `fs/namei.c`, including mount crossing and `..` at the root. | §1 |
| [Shared Subtrees](https://docs.kernel.org/filesystems/sharedsubtree.html) | The original design of mount propagation, with state-transition rules. | §4 |
| [Tmpfs](https://docs.kernel.org/filesystems/tmpfs.html) | `size`, `nr_inodes`, `mode`, and memory accounting of tmpfs. | §5 |
| [Devpts Filesystem](https://docs.kernel.org/filesystems/devpts.html) | Multi-instance devpts and `ptmx`. | §5 |
| [sysfs](https://docs.kernel.org/filesystems/sysfs.html) | Why sysfs exists and how it maps kernel objects. | §5 |
| [Documentation for /proc/sys](https://docs.kernel.org/admin-guide/sysctl/index.html) | The global and per-namespace tunables exposed under `/proc/sys`. | §5 |
| [Linux allocated devices](https://docs.kernel.org/admin-guide/devices.html) | The registry of device major/minor numbers. | §5 |

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`path_resolution(7)`](https://man7.org/linux/man-pages/man7/path_resolution.7.html) | User-space view of path lookup in two pages. | §1 |
| [`inode(7)`](https://man7.org/linux/man-pages/man7/inode.7.html), [`unlink(2)`](https://man7.org/linux/man-pages/man2/unlink.2.html) | Inode fields; when file data is really freed. | §1 |
| [`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html) | Every `MS_*` flag, bind/remount/propagation rules. | §2–4 |
| [`umount(2)`](https://man7.org/linux/man-pages/man2/umount.2.html) | `MNT_DETACH` and `EBUSY` semantics. | §2 |
| [`mount_setattr(2)`](https://man7.org/linux/man-pages/man2/mount_setattr.2.html) | Recursive, atomic mount attribute changes; idmapped mounts. | §3 |
| [`proc_pid_mountinfo(5)`](https://man7.org/linux/man-pages/man5/proc_pid_mountinfo.5.html) | Specification of the `mountinfo` format. | §2 |
| [`mount_namespaces(7)`](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html) | Shared subtree defaults, peer groups, `mountinfo` tags; also Chapter 03. | §4 |
| [`mount(8)`](https://man7.org/linux/man-pages/man8/mount.8.html), [`findmnt(8)`](https://man7.org/linux/man-pages/man8/findmnt.8.html) | The tools used in every lab. | Labs |
| [`mknod(2)`](https://man7.org/linux/man-pages/man2/mknod.2.html), [`pty(7)`](https://man7.org/linux/man-pages/man7/pty.7.html) | Device nodes and pseudo-terminals. | §5 |

## Kernel source (primary)

- [`fs/namei.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namei.c) —
  path walking. Look at `link_path_walk()`, `follow_dotdot()`, and
  `traverse_mounts()`.
- [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c) —
  `path_mount()` dispatches to `do_new_mount()`, `do_loopback()` (bind),
  `do_remount()`, `do_change_type()` (propagation), and `do_move_mount()`.
- [`fs/pnode.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/pnode.c) —
  `propagate_mnt()` and `propagate_umount()`: propagation to peers and slaves.
- [`fs/mount.h`](https://elixir.bootlin.com/linux/v6.12/source/fs/mount.h) —
  `struct mount`, with fields `mnt_parent`, `mnt_mountpoint`, `mnt_share`
  (peers), `mnt_slave_list`, and `mnt_master`.

## Specifications and orchestration docs (for connections)

- OCI Runtime Specification, [config-linux.md: Default Filesystems and Default Devices](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md)
  — the minimal `/proc`, `/sys`, `/dev` set a runtime must provide.
- Kubernetes documentation, [Volumes: mount propagation](https://kubernetes.io/docs/concepts/storage/volumes/#mount-propagation)
  and [ConfigMaps](https://kubernetes.io/docs/concepts/configuration/configmap/)
  — real consequences of propagation types and single-file bind mounts.

## Secondary sources (selected)

- Michael Kerrisk, LWN,
  ["Mount namespaces and shared subtrees"](https://lwn.net/Articles/689856/)
  and ["Mount namespaces, mount propagation, and unbindable mounts"](https://lwn.net/Articles/690679/)
  (2016) — hands-on explanations of propagation; they also bridge into
  Chapter 03.
- David Howells, LWN, ["A new API for mounting filesystems"](https://lwn.net/Articles/753473/)
  (2018) — the motivation for `fsopen()`/`fsmount()`/`move_mount()`.
- Michael Kerrisk, *The Linux Programming Interface*, chapters 14 (file
  systems, including mount flags and bind mounts) and 18 (directories and
  links). Pre-dates the new mount API but explains the classic model very
  carefully.
