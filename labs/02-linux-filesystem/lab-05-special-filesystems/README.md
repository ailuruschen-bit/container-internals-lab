# Lab 05 — Special Filesystems and Device Nodes

## Goal

Produce evidence that:

1. a device node's behavior is defined by its type and major/minor numbers,
   not its name or location;
2. creating device nodes requires privilege, and `nodev` prevents using them;
3. procfs and sysfs can be mounted again anywhere, and sysfs can be read-only;
4. a new devpts instance does not contain the host's terminals;
5. a tmpfs `size=` limit is enforced.

## Prerequisites

- Linux VM with `sudo`, `util-linux`, `coreutils`.
- Read: [5. Special filesystems](../../../docs/02-linux-filesystem/05-special-filesystems.md).

## Experiment

```bash
mkdir -p /tmp/speclab/{dev,dev-nodev,proc,sys,pts,shm} && cd /tmp/speclab
sudo mount -t tmpfs -o mode=755 lab-dev dev        # a tmpfs to hold device nodes
```

### Part A — A device is a (type, major, minor), not a name

```bash
ls -l /dev/null /dev/zero
mknod dev/mynull c 1 3; echo "unprivileged mknod exit code: $?"
sudo mknod -m 666 dev/mynull c 1 3
sudo mknod -m 666 dev/definitely-not-zero c 1 5
echo "this text disappears" > dev/mynull
wc -c < dev/mynull
head -c 8 dev/definitely-not-zero | od -An -tx1
stat -c '%n type=%F major=%t minor=%T' /dev/null dev/mynull
```

**Predict first.** What does reading `definitely-not-zero` return?

### Part B — `nodev`

```bash
sudo mount -t tmpfs -o nodev,mode=755 lab-dev-nodev dev-nodev
sudo mknod -m 666 dev-nodev/mynull c 1 3; echo "mknod exit code: $?"
ls -l dev-nodev/mynull
echo test > dev-nodev/mynull; echo "open exit code: $?"
```

**Predict first.** Does `nodev` prevent *creating* the node, *opening* it, or
both?

### Part C — procfs and sysfs are mountable instances

```bash
sudo mount -t proc lab-proc proc
ls proc | grep -E '^[0-9]+$' | wc -l
ls /proc | grep -E '^[0-9]+$' | wc -l
sudo mount -t sysfs -o ro lab-sys sys
ls sys/class/net
cat sys/class/net/lo/mtu
echo 1500 | sudo tee sys/class/net/lo/mtu; echo "exit code: $?"
```

### Part D — A new devpts instance

```bash
ls /dev/pts
tty
sudo mount -t devpts -o newinstance,ptmxmode=0666,mode=0620 lab-pts pts
ls -l pts
```

**Predict first.** Will `pts` contain the terminal your shell is using?

### Part E — tmpfs size limit

```bash
sudo mount -t tmpfs -o size=10m lab-shm shm
df -h shm
sudo dd if=/dev/zero of=shm/fill bs=1M count=20; echo "exit code: $?"
ls -lh shm/fill
```

### Part F — The host's `/dev` vs what a container needs

```bash
ls /dev | wc -l
findmnt -o TARGET,FSTYPE,OPTIONS /dev /dev/pts /dev/shm /dev/mqueue
ls -l /dev | grep '^b'           # block devices: disks a container must not see
```

### Cleanup

```bash
cd /tmp
sudo umount /tmp/speclab/{dev,dev-nodev,proc,sys,pts,shm}
findmnt -R /tmp/speclab || echo "no mounts left"
sudo rm -rf /tmp/speclab
```

## Expected observations

**Part A.** Unprivileged `mknod` fails with `Operation not permitted`. With
`sudo`, `dev/mynull` swallows writes (`wc -c` prints `0`), and
`definitely-not-zero` returns `00 00 00 00 00 00 00 00`. `stat` shows the
same `major=1 minor=3` for `/dev/null` and `dev/mynull` (printed in hex by
`%t`/`%T`).

**Part B.** `mknod` **succeeds** and `ls -l` shows a character device, but
writing fails with `Permission denied`: `nodev` prevents *opening*, not
creating.

**Part C.** Both counts are equal (possibly off by one or two because of
short-lived processes): a second procfs in the same PID namespace shows the
same processes. `ls sys/class/net` lists the host's interfaces. Writing `mtu`
through the read-only sysfs fails with `Read-only file system`.

**Part D.** `/dev/pts` contains numbered entries such as `0` and `1`, and
`tty` prints one of them. The new `pts` instance contains only `ptmx`; your
terminal is not there.

**Part E.** `df` shows a size of 10 M. `dd` stops with
`No space left on device` after about 10 MB, and the file is about 10 M.

**Part F.** Typically 150–250 entries in `/dev`. `/dev` is `devtmpfs`,
`/dev/pts` is `devpts`, `/dev/shm` is `tmpfs`, `/dev/mqueue` is `mqueue`.
Block devices such as `vda`, `sda`, or `nvme0n1` are listed.

## Why this happens

- **A.** `open()` on a device inode dispatches to the driver registered for its
  major/minor number. `mknod()` requires `CAP_MKNOD`.
- **B.** The VFS checks the mount's `MNT_NODEV` flag when opening a device
  inode, not when creating it.
- **C.** Each `mount -t proc` creates a view tied to the mounter's PID namespace;
  `ro` is a per-mount flag enforced by the VFS.
- **D.** Every devpts mount with `newinstance` has its own pty index space.
- **E.** tmpfs accounts for its pages against the `size=` limit.

## Connection to containers

- Part A is why runtimes restrict `CAP_MKNOD` and use the cgroup device
  controller: a container that can create a node for the host disk's
  major/minor could read it.
- Part B is why container `/dev` and volume mounts are often `nodev`.
- Part C previews Chapter 03: a procfs mounted in a *new* PID namespace will
  show different processes.
- Part D is how `docker run -it` gives a container its own terminal devices.
- Part E is `--shm-size`.

## Questions to think about

1. If a container has `CAP_MKNOD` but its `/dev` tmpfs is mounted `nodev`, can
   it use a node it creates? Where else could it create one?
2. Why is it not enough to delete `/dev/vda` from a container's `/dev` to
   protect the host disk?
3. Which sysctl settings would you expect to be safe to change from inside a
   container, and which not? Use the per-namespace vs global distinction.
4. A JVM in a container crashes with an error writing to `/dev/shm`. What would
   you check first?
