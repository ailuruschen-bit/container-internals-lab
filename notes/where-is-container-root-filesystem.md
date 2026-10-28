# Linux Filesystems, Mounts, and Mount Namespaces: What I Learned

**English** | [中文](zh/where-is-container-root-filesystem.md) | [日本語](ja/where-is-container-root-filesystem.md)

## 1. Rethinking the Linux Directory Tree

Coming from the graphical view of Windows Explorer, it is easy to assume that an operating system has a single, fixed directory tree in which every process accesses and manipulates files.

Linux does not actually work that way:

- The root directory `/` is not the same thing as a physical disk.
- A complete directory tree can be assembled from multiple filesystems.
- Different processes do not necessarily see the same directory tree.
- Some directories correspond to real data on disk, while others have their contents generated dynamically by the kernel.

For example:

```text
/
├── etc/      regular filesystem
├── home/     regular filesystem
├── proc/     procfs (virtual filesystem)
└── sys/      sysfs (virtual filesystem)
```

The Linux directory tree is therefore a logical view that the kernel builds out of filesystems and the mount mechanism, not a physical tree that exists in some fixed form.

## 2. Mounts and Mount Namespaces

### Mount

Mounting attaches a filesystem to the existing directory tree at a given location.

For example:

```bash
mount /dev/sdb1 /data
```

This command mounts the filesystem on `/dev/sdb1` at `/data`. From then on, whenever `/data` is accessed, the kernel follows this mount relationship into the corresponding filesystem.

Mounting copies no file contents. It only establishes an association between a path in the directory tree and a filesystem.

### Mount Namespace

A mount namespace can be thought of as a view of mount relationships shared by a group of processes.

Ordinary processes on a Linux host usually live in the initial mount namespace, while container processes can use a mount namespace of their own:

```text
Namespace A          Namespace B
    /                     /
    └── data/             └── data/
         │                     │
    Filesystem X          Filesystem Y
```

Two processes accessing the same path, `/data`, can end up in different filesystems.

Points to keep in mind:

- A namespace does not automatically copy the underlying filesystem.
- A namespace does not automatically change a process's root directory.
- Different namespaces can reference the same underlying file resources.
- Whether a mount event propagates to other namespaces also depends on mount propagation types such as `shared` and `private`.

## 3. A Process's `root` and `cwd`

The Linux kernel maintains two important filesystem-context attributes for each process:

- `root`: the starting point for resolving absolute paths.
- `cwd`: the starting point for resolving relative paths.

Both are normally inherited from the parent process.

For example:

```bash
cd /home/user
```

This changes the shell process's `cwd`.

The following system call, on the other hand, changes the process's `root` to `/some/rootfs`, so that absolute paths are resolved from there from then on:

```c
chroot("/some/rootfs");
```

`chroot` changes only the starting point of path resolution; on its own, it is not a complete security isolation mechanism. Container runtimes also combine mount namespaces, mount configuration, privilege restrictions, and mechanisms such as `pivot_root` to prepare the container environment.

Three independent concepts therefore need to be kept apart:

```text
root             starting point for absolute paths
cwd              starting point for relative paths
Mount Namespace  the visible set of filesystem mounts
```

Two processes in the same mount namespace can still have different `root` and `cwd` values. Conversely, even if two processes both call their root directory `/`, the filesystem contents they see may differ.

## 4. OverlayFS and the Docker Container's Root Directory

"Overlay" means to lay one thing over another. OverlayFS merges multiple directory layers into a single, unified filesystem view.

Its main components are:

- `lowerdir`: one or more base layers, typically used as read-only layers.
- `upperdir`: the writable layer, which stores new and modified files.
- `workdir`: a working directory that OverlayFS needs for its internal operations.
- `merged`: the merged view presented to users once the mount is in place.

```text
lowerdir ──┐
           ├── OverlayFS ── merged ── container /
upperdir ──┘
```

When looking up a file, OverlayFS checks `upperdir` first and then `lowerdir`. Directories with the same name in both layers may also be merged in the resulting view.

When a container needs to modify a file that exists only in `lowerdir`, OverlayFS typically copies the file up to `upperdir` first and then modifies that upper copy. This is Copy-on-Write (CoW).

Each Docker container usually gets its own writable layer, so multiple containers can share an image's base layers while keeping their own changes isolated. A container process sees the directory tree as merged by OverlayFS and does not need to know which layer a given file actually comes from.

Note also that `/proc` inside a container is not a copied set of kernel state. The container runtime mounts procfs in the container's mount view, and the same Linux kernel presents process information according to the PID namespace associated with that procfs instance.

This is why a process inside a container can see itself as PID 1 while the host identifies the very same process by a different PID. Both sides read the same process objects maintained by the kernel; they simply see different PIDs and a different set of processes.

## 5. The Resulting Mental Model

The Linux kernel manages the underlying filesystem resources, as well as the mount relationships that attach those resources to the directory tree. Each process then resolves paths through its own filesystem context:

```text
Linux kernel
    │
    ├── Underlying filesystem resources
    │     ├── ext4
    │     ├── OverlayFS
    │     ├── procfs
    │     └── sysfs
    │
    └── Mount Namespaces
          │
          ├── Mount view A
          │      └── Process A (root / cwd)
          │
          └── Mount view B
                 └── Process B (root / cwd)
```

Key takeaways:

1. The directory tree is not a single, fixed physical tree that every process must share.
2. Mounts establish the relationship between filesystems and the directory tree.
3. Mount namespaces give different groups of processes different views of the mount relationships.
4. `root` and `cwd` determine where a process starts resolving paths.
5. Docker combines mount namespaces, root directory switching, OverlayFS, and other mechanisms to give container processes an independent filesystem environment.

**At its core, container filesystem isolation does not copy an entire computer. It uses one and the same Linux kernel to present different filesystem views to different groups of processes.**
