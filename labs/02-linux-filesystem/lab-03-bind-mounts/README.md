# Lab 03 — Bind Mounts: Directories, Files, Recursion, and Read-Only

## Goal

Produce evidence that:

1. a bind mount exposes the same inodes at a second path;
2. `mountinfo` field 4 reveals the source directory of a bind mount;
3. a plain bind mount does not include submounts, while a recursive one does;
4. a read-only bind mount is read-only only at the new location;
5. a single-file bind mount pins an inode and does not follow a rename.

## Prerequisites

- Linux VM with `sudo` and `util-linux`.
- Read: [3. Bind mounts](../../../docs/02-linux-filesystem/03-bind-mounts.md).

## Experiment

```bash
mkdir -p /tmp/bindlab/{src,dst,dst-ro,tree,tree-plain,tree-rec} && cd /tmp/bindlab
echo "hello" > src/greeting.txt
```

### Part A — Same inode, two paths

```bash
sudo mount --bind src dst
stat -c '%n inode=%i dev=%d' src/greeting.txt dst/greeting.txt
echo "written through dst" > dst/greeting.txt
cat src/greeting.txt
grep bindlab /proc/self/mountinfo
```

### Part B — Plain vs recursive

Create a submount inside `tree`:

```bash
mkdir -p tree/inner
sudo mount -t tmpfs inner-tmpfs tree/inner
echo "inside the submount" | sudo tee tree/inner/file.txt >/dev/null
```

**Predict first.** What will `ls tree-plain/inner` and `ls tree-rec/inner`
show?

```bash
sudo mount --bind  tree tree-plain
sudo mount --rbind tree tree-rec
ls tree-plain/inner
ls tree-rec/inner
findmnt -R /tmp/bindlab
```

### Part C — Read-only in one place only

```bash
sudo mount --bind -o ro src dst-ro
echo "via original" >> src/greeting.txt && echo "original path: writable"
echo "via ro bind" >> dst-ro/greeting.txt; echo "exit code: $?"
sudo sh -c 'echo "as root" >> /tmp/bindlab/dst-ro/greeting.txt'; echo "exit code: $?"
grep dst-ro /proc/self/mountinfo
```

Now check the recursive case:

```bash
sudo mount --rbind -o ro tree /tmp/bindlab/dst       # dst already has a mount; this stacks
touch dst/x;            echo "top mount exit code: $?"
sudo touch dst/inner/x; echo "submount exit code: $?"
findmnt -R -o TARGET,OPTIONS /tmp/bindlab/dst
```

### Part D — A file bind mount pins an inode

```bash
echo "config v1" > config.txt
touch mounted-config.txt
sudo mount --bind config.txt mounted-config.txt
cat mounted-config.txt

echo "config v2 (in place)" > config.txt
cat mounted-config.txt

echo "config v3 (rename)" > config.txt.new && mv config.txt.new config.txt
cat config.txt
cat mounted-config.txt
```

**Predict first.** What will the last two `cat` commands print?

### Cleanup

```bash
cd /tmp
sudo umount -R /tmp/bindlab/dst /tmp/bindlab/dst /tmp/bindlab/dst-ro \
    /tmp/bindlab/tree-rec /tmp/bindlab/tree-plain /tmp/bindlab/tree/inner \
    /tmp/bindlab/mounted-config.txt 2>/dev/null
findmnt -R /tmp/bindlab || echo "no mounts left"
rm -rf /tmp/bindlab
```

If `findmnt` still lists mounts, run `sudo umount -R /tmp/bindlab/<path>` for
each one.

## Expected observations

**Part A.** Both paths show the same inode and device. `src/greeting.txt` shows
`written through dst`. The `mountinfo` line for `dst` has field 4 equal to the
path of `src` inside its filesystem (for example `/tmp/bindlab/src`, or
`/bindlab/src` if `/tmp` is its own tmpfs).

**Part B.** `tree-plain/inner` is **empty**; `tree-rec/inner` contains
`file.txt`. `findmnt -R` shows `inner-tmpfs` under `tree/inner` and under
`tree-rec/inner`, but not under `tree-plain`.

**Part C.** Writing through `src` works. Writing through `dst-ro` fails with
`Read-only file system`, **also as root**. The `mountinfo` line shows `ro`
in the per-mount options (field 6), while field 11 (super block options) is
still `rw`.

For the recursive case: `touch dst/x` fails with `Read-only file system`, but
`sudo touch dst/inner/x` **succeeds** on older util-linux versions, because
only the top mount was remounted read-only. On util-linux 2.38+ with kernel
5.12+, `mount` may apply `ro` recursively using `mount_setattr()`; then both
fail. `findmnt` shows which mounts carry `ro`.

**Part D.**

```text
config v1
config v2 (in place)
config v3 (rename)
config v2 (in place)
```

After the rename, the original name points to a new inode, but the bind mount
still shows the old one.

## Why this happens

- **A.** A bind mount is a new `struct mount` referencing the same super block
  and a non-root dentry.
- **B.** `MS_BIND` clones one mount. `MS_BIND|MS_REC` clones the whole subtree
  of mounts below the source.
- **C.** `ro` is stored on the mount, and the VFS checks it before the
  filesystem is asked to write. Root has no exception for this check.
- **D.** The file bind mount referenced a dentry/inode, not the name. `rename()`
  changed which inode the name refers to.

## Connection to containers

- Part A is a container volume.
- Part B shows why a runtime must decide carefully between `bind` and `rbind`
  for host paths: `rbind` can drag host submounts into the container.
- Part C is exactly `-v /host/path:/container/path:ro`, and the recursive case
  explains why "read-only" host mounts historically did not always make
  submounts read-only. Newer runtimes and Kubernetes offer explicit recursive
  read-only mounts.
- Part D is the mechanism behind stale `/etc/resolv.conf` or `subPath`
  configuration files in containers.

## Questions to think about

1. Why would a symlink from `dst` to `src` not work if the process that uses
   `dst` later changes its root directory?
2. In Part C, `dst-ro` is read-only. Can a process with the right privileges
   make it writable again? What would prevent that inside a container?
3. How would you make a file appear inside a mounted directory without
   modifying the directory's source on the host?
4. A runtime wants `/proc/sys` read-only inside a container but `/proc`
   writable. Using only operations from this lab, how could it do that?
