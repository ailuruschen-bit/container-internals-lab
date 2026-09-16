# Lab 02 — Mounts, Hidden Directories, and mountinfo

## Goal

Produce evidence that:

1. mounting a filesystem on a directory hides its previous contents without
   deleting them;
2. a process already inside the hidden directory keeps access to it;
3. mounts stack, and each one is a separate line in `mountinfo`;
4. per-mount options (`noexec`, `ro`) are enforced by the VFS;
5. unmounting a busy mount fails, while a lazy unmount detaches it immediately.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`findmnt`), `coreutils`.
- Read: [2. Mounts and the mount table](../../../docs/02-linux-filesystem/02-mounts.md).

## Background

`mount -t tmpfs NAME DIR` creates a new tmpfs instance and mounts it at `DIR`.
The `NAME` is just a label shown as the source. Every command below that
modifies mounts is a `mount()` or `umount2()` system call made by the `mount`
and `umount` tools.

## Experiment

```bash
mkdir -p /tmp/mntlab/dir && cd /tmp/mntlab
echo "I was here before the mount" > dir/original.txt
```

### Part A — Mounting hides, it does not delete

**Predict first.** After mounting a tmpfs on `dir`, what will `ls dir` show?
What happens to `original.txt`?

```bash
sudo mount -t tmpfs lab-tmpfs-1 dir
ls -la dir
echo "I live in tmpfs #1" | sudo tee dir/tmpfs1.txt
findmnt dir
```

### Part B — A process inside the hidden directory

Open a **second terminal** and run:

```bash
cd /tmp/mntlab/dir && ls        # this shell's cwd is now the tmpfs root
```

Back in the first terminal, stack a second tmpfs:

```bash
sudo mount -t tmpfs lab-tmpfs-2 dir
echo "I live in tmpfs #2" | sudo tee dir/tmpfs2.txt
ls dir
```

In the second terminal:

```bash
ls              # relative to cwd
ls /tmp/mntlab/dir
```

**Predict first.** Will the two `ls` commands in the second terminal print the
same thing?

### Part C — Read the mount table

```bash
grep lab-tmpfs /proc/self/mountinfo
findmnt -o ID,PARENT,TARGET,SOURCE,FSTYPE,OPTIONS --target /tmp/mntlab/dir
```

Identify, in the `mountinfo` lines, the mount ID, parent ID, device number,
mount options, and source for both tmpfs instances.

### Part D — Per-mount options

```bash
cp /bin/true dir/mytrue
dir/mytrue && echo "exec allowed"
sudo mount -o remount,noexec dir
dir/mytrue; echo "exit code: $?"
sudo mount -o remount,ro dir
sudo touch dir/new-file; echo "exit code: $?"
grep lab-tmpfs-2 /proc/self/mountinfo
sudo mount -o remount,rw,exec dir
```

### Part E — Busy mounts and lazy unmount

The second terminal's working directory is still inside tmpfs #1, but tmpfs #2
is on top and not busy. Unmount it:

```bash
sudo umount dir
ls dir                           # tmpfs #1 again
sudo umount dir; echo "exit code: $?"
```

**Predict first.** Why might this second `umount` fail?

```bash
sudo umount --lazy dir
ls dir                           # original.txt is back
grep lab-tmpfs /proc/self/mountinfo || echo "no lab mounts in the table"
```

In the second terminal:

```bash
ls; cat tmpfs1.txt
cd /tmp
```

Clean up in the first terminal: `cd /tmp && rm -rf /tmp/mntlab`.

## Expected observations

**Part A.** `ls -la dir` shows an empty directory. `original.txt` is not
listed but has not been deleted. `findmnt` shows `SOURCE lab-tmpfs-1`,
`FSTYPE tmpfs`.

**Part B.** Different results. `ls /tmp/mntlab/dir` shows `tmpfs2.txt`. Plain
`ls` shows `tmpfs1.txt`, because that shell's working directory still refers
to the root dentry of tmpfs #1.

**Part C.** Two lines, for example:

```text
512 34 0:61 / /tmp/mntlab/dir rw,relatime shared:280 - tmpfs lab-tmpfs-1 rw,inode64
513 512 0:62 / /tmp/mntlab/dir rw,relatime shared:281 - tmpfs lab-tmpfs-2 rw,inode64
```

The second mount's parent ID is the first mount's ID. Both have the same mount
point but different device numbers. Whether you see `shared:N` depends on the
propagation of `/tmp` ([section 4](../../../docs/02-linux-filesystem/04-mount-propagation.md)).

**Part D.** `exec allowed`; after `noexec`, running `dir/mytrue` fails with
`Permission denied` and exit code 126. After `ro`, `touch` fails with
`Read-only file system` even as root. The `mountinfo` line shows `ro,noexec` in
field 6.

**Part E.** The first `umount` succeeds. The second fails with
`target is busy`, because a process (your second shell) has its working
directory inside tmpfs #1. After `umount --lazy`, the mount disappears from the
table and `original.txt` becomes visible again, yet the second terminal can
still list and read `tmpfs1.txt` until it leaves.

## Why this happens

- **A.** Path resolution reaching `dir` sees a mount and continues into the
  tmpfs root. The ext4 (or tmpfs) directory underneath is unchanged.
- **B.** A working directory is a `(mount, dentry)` reference, not a string.
  The old reference does not re-resolve when a new mount appears.
- **C.** Each `mount()` created a `struct mount` with its own ID, attached to
  the parent mount at the same dentry.
- **D.** The VFS checks per-mount flags (`MNT_NOEXEC`, `MNT_READONLY`) before
  calling into the filesystem; root does not bypass them.
- **E.** `umount2()` without flags refuses to detach a mount that is referenced.
  `MNT_DETACH` removes it from the tree and frees it when the last reference is
  dropped.

## Connection to containers

- A container runtime builds a new mount tree with the same system calls you
  used here: create mounts (`proc`, `tmpfs`, `sysfs`), set per-mount options,
  and remount read-only where needed.
- Part B and Part E together explain why runtimes use a lazy unmount to discard
  the old root after `pivot_root` (Chapter 07): processes and references may
  still exist, but the mount must disappear from the container's view.
- Part D is the mechanism behind "read-only root filesystem" and
  `noexec`/`nosuid` mount options for container volumes.

## Questions to think about

1. In Part B, a program in the second terminal holds references to a mount that
   is no longer reachable by path. What risk does that create for isolation if
   the program is later moved into a container?
2. Why does `noexec` not prevent `bash dir/script.sh`? (Hint: which file is
   actually passed to `execve()`?)
3. What would happen to the data of `tmpfs1.txt` after the second terminal
   leaves the directory?
4. Find a running container on any machine and compare
   `sudo cat /proc/<container-pid>/mountinfo` with the host's
   `/proc/self/mountinfo`. How many mounts does each have?
