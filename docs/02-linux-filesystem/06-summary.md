# 6. Chapter Summary: What a Runtime Must Build for a New Root

## The model you should now have

Every path lookup is performed **for a specific process**, starting from that
process's root directory (or working directory) and walking a **mount tree**.
The VFS converts the path into a `(mount, dentry)` pair, and from there into an
inode on some filesystem instance.

```text
process ── fs_struct ──► root directory, working directory      (per process)
   │
   └── mount namespace ──► mount tree                            (per namespace, Ch. 03)
                               │
                               ▼
                      mounts: (filesystem instance, root dentry, mount point,
                               per-mount flags, propagation type)
                               │
                               ▼
                      VFS objects: super_block → dentry → inode → data
```

The operations that shape what a process sees:

| Operation | System call | What changes | Section |
|---|---|---|---|
| Mount a filesystem | `mount(src, dst, type, 0, data)` | adds a mount to the tree | 2 |
| Bind mount | `mount(src, dst, NULL, MS_BIND[\|MS_REC], NULL)` | exposes an existing dentry elsewhere | 3 |
| Change per-mount flags | `mount(..., MS_REMOUNT\|MS_BIND\|MS_RDONLY...)` | `ro`, `nosuid`, `nodev`, `noexec` | 2, 3 |
| Change propagation | `mount(NULL, dst, NULL, MS_SLAVE\|MS_REC, NULL)` | how mount events flow | 4 |
| Unmount | `umount2(dst, 0 \| MNT_DETACH)` | removes a mount (lazily) | 2 |
| Create a device node | `mknod(path, S_IFCHR\|mode, makedev(1,3))` | a door to a driver | 5 |
| Change root directory | `chroot()` / `pivot_root()` | the start of every absolute path | Ch. 07 |

## The sentence to remember

> What a process sees at `/` is decided by two per-process inputs: its root
> directory and its mount tree. The files themselves are ordinary VFS objects.

## What has *not* been explained yet

- **How one process gets a different mount tree** from the rest of the system.
  So far every experiment changed the single mount tree shared by all
  processes; that is why the labs used `/tmp` scratch directories. The mount
  namespace (Chapter 03) removes this limitation.
- **How the root directory is replaced safely.** `chroot()` and `pivot_root()`
  are Chapter 07.
- **Where the image's files come from.** Layered, copy-on-write root
  filesystems are OverlayFS, Chapter 08.

## Self-check questions

1. What is the difference between an inode and a dentry? Which one does a hard
   link add? (§1)
2. Why does `..` at `/` stay at `/`, and whose `/` is it? (§1)
3. Explain each of the 11 fields of a `mountinfo` line. Which field reveals a
   bind mount? (§2)
4. A process has its working directory in a directory that is then over-mounted.
   What does `ls` show for that process, and why? (§2)
5. Why does a single-file bind mount show stale content after an editor saves
   the source file? (§1, §3)
6. What is the difference between `--bind` and `--rbind`, and why is `--rbind /`
   dangerous? (§3)
7. On a systemd host, a container's mount namespace is created without changing
   propagation. A mount made inside the container appears on the host. Explain
   why, in terms of peer groups. (§4)
8. Why must a container's `/dev` not be the host's devtmpfs? What does a runtime
   mount instead? (§5)

## Next: Chapter 03 — Namespaces

With processes (Chapter 01) and mounts (Chapter 02) understood, Chapter 03 can
explain namespaces as "per-process views of global kernel resources": first
what each global resource is, then how `clone()`, `unshare()`, and `setns()`
create or join a separate view, with experiments for the UTS, PID, mount,
IPC, network, user, and cgroup namespaces.
