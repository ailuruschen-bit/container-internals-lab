# Lab 01 — Inodes, Names, and Path Resolution

## Goal

Produce evidence that:

1. names (dentries) and file objects (inodes) are separate;
2. an inode number is only unique within one filesystem instance;
3. file data survives deleting its last name while a process holds it open;
4. replacing a file by rename creates a new inode, while writing in place does
   not;
5. path resolution crosses mount points and is performed relative to the
   process's root directory.

## Prerequisites

- Linux VM with `coreutils`, `util-linux` (`findmnt`, `namei`), and `python3`.
- Read: [1. The VFS and path resolution](../../../docs/02-linux-filesystem/01-vfs-and-path-resolution.md).

No root access is needed.

## Background

`stat` prints the device ID (`Device:`), inode number (`Inode:`), and link
count (`Links:`) of a path. `stat -f` prints information about the filesystem
instance containing the path.

## Experiment

Work in a scratch directory:

```bash
mkdir -p /tmp/vfslab && cd /tmp/vfslab
```

### Part A — Two names, one inode

```bash
echo "version 1" > a.txt
ln a.txt b.txt                         # hard link: a second name
stat -c 'name=%n inode=%i links=%h mode=%A' a.txt b.txt
chmod 600 b.txt
stat -c 'name=%n inode=%i links=%h mode=%A' a.txt
rm a.txt
stat -c 'name=%n inode=%i links=%h' b.txt
cat b.txt
```

**Predict first.** After `chmod 600 b.txt`, what is the mode of `a.txt`? After
`rm a.txt`, does `b.txt` still contain data?

### Part B — Inode numbers are per filesystem instance

```bash
stat -c 'dev=%d inode=%i fs=%m %n' / /tmp /proc /sys /dev
stat -f -c 'type=%T %n' / /tmp /proc /sys /dev
ls -di / /proc /sys /dev
```

### Part C — Delete a file that is still open

```bash
python3 -c 'import os; open("big.dat","wb").write(os.urandom(50*1024*1024))'
df -h --output=used /tmp
exec 5< big.dat                        # the shell holds it open as fd 5
rm big.dat
ls -l /proc/$$/fd/5
df -h --output=used /tmp
head -c 16 <&5 | od -An -tx1           # data still readable
exec 5<&-                              # close the last reference
df -h --output=used /tmp
```

If `/tmp` is not a separate filesystem on your machine, `df` values move
by 50 MB against a larger total. Look at the change, not the absolute value.

### Part D — Write in place vs replace by rename

```bash
echo "original" > config.txt
stat -c 'inode=%i' config.txt
echo "edited in place" > config.txt          # truncate and write the same inode
stat -c 'inode=%i' config.txt
echo "replaced" > config.txt.new && mv config.txt.new config.txt   # atomic replace
stat -c 'inode=%i' config.txt
```

**Predict first.** Which of the three `stat` calls print the same inode
number?

### Part E — Resolution crosses mounts, step by step

```bash
namei -l /proc/self/root/etc/hostname
findmnt -T /tmp
findmnt -T /proc/self
readlink /proc/self/root
```

## Expected observations

**Part A.** Both names show the same inode and `links=2`. After `chmod`, `a.txt`
also shows `-rw-------`. After `rm a.txt`, `b.txt` has `links=1` and still
prints `version 1`.

**Part B.** Different `dev` values for `/`, `/proc`, `/sys`, and `/dev` (and for
`/tmp` if it is a tmpfs). Types include `ext2/ext3` (ext4 is reported this way
by `stat -f`) or `xfs` for `/`, `proc`, `sysfs`, `tmpfs` or `devtmpfs`. Root
directories of different filesystems often have small, repeated inode numbers
(for example `2` for ext4's root and `1` for `/proc`).

**Part C.** After `rm`, `ls -l /proc/$$/fd/5` shows `.../big.dat (deleted)`,
and `df` usage **does not drop**. The bytes are still readable. Only after
`exec 5<&-` does usage drop by about 50 MB.

**Part D.** The first two inode numbers are identical; the third is different.

**Part E.** `namei` prints one line per component, and `root` is shown as a
symlink to `/`. `findmnt -T /proc/self` reports target `/proc`, source `proc`,
and type `proc`: resolution of `/proc/self` left the root filesystem and
entered the procfs mount. `readlink /proc/self/root` prints `/`.

## Why this happens

- **A, D.** Permissions live in the inode shared by all names. `>` opens the
  existing inode with `O_TRUNC`; `mv` calls `rename()`, which points the name
  at a different inode.
- **B.** Each filesystem instance numbers its own inodes. The kernel
  distinguishes files by (device, inode).
- **C.** `unlink()` removed the last dentry, bringing `nlink` to 0. The inode is
  freed only when the open file description held by fd 5 is released.
- **E.** At the `proc` component, the lookup found a mount point and continued
  in the root of the procfs instance.

## Connection to containers

- Part C explains a common production puzzle: a container deletes a large log
  file, but disk usage does not fall, because the application still holds it
  open.
- Part D explains why a **single-file bind mount** (Lab 03) can show stale
  content: tools that save by rename create a new inode, but the bind mount
  still refers to the old one. This affects configuration files mounted into
  containers, including Kubernetes ConfigMaps mounted with `subPath`.
- Part E is the mechanism a container relies on: the same path string resolves
  differently depending on the mount tree and root directory of the calling
  process.

## Questions to think about

1. Why can hard links not cross filesystem boundaries? Relate your answer to
   Part B.
2. In Part C, which kernel object keeps the data alive: the dentry, the inode,
   or the open file description? Which one does `/proc/$$/fd/5` point to?
3. Editors like `vim` may write by rename. What would you observe if a
   container process bind-mounts a single file and you edit it on the host with
   such an editor?
4. `ls -di /` in a container usually shows a different device than on the host.
   What does that tell you about the container's root filesystem?
