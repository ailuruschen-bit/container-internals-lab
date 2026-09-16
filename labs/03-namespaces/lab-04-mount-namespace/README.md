# Lab 04 — Mount Namespace: Private Trees, Shared Files, Propagation

## Goal

Produce evidence that:

1. a mount made in a new mount namespace with private propagation is invisible
   outside;
2. files on copied mounts are **not** isolated;
3. per-mount flags changed inside do not affect the host's mount;
4. without changing propagation, mounts leak between host and namespace on a
   systemd host;
5. `rslave` propagation gives one-way visibility (host → namespace);
6. `nsenter --mount` joins an existing mount tree.

## Prerequisites

- Linux VM with `sudo`, `util-linux`. A systemd-based distribution shows Part D
  most clearly.
- Read: [4. Mount namespace](../../../docs/03-namespaces/04-mount-namespace.md)
  and Chapter 02 §4.

## Background

`unshare --mount` defaults to `--propagation private` (it runs
`mount --make-rprivate /` inside the new namespace). `--propagation unchanged`
keeps whatever the copy inherited, and `--propagation slave` runs the
equivalent of `mount --make-rslave /`.

Use two terminals throughout: **H** (host) and **N** (namespace).

```bash
sudo mkdir -p /mnt/nslab
```

## Experiment

### Part A — A private mount

N:

```bash
sudo unshare --mount bash
readlink /proc/$$/ns/mnt
mount -t tmpfs ns-private /mnt/nslab
echo "inside" > /mnt/nslab/file.txt
findmnt /mnt/nslab
echo $$
```

H:

```bash
readlink /proc/self/ns/mnt
findmnt /mnt/nslab || echo "not mounted on the host"
ls /mnt/nslab
```

### Part B — Files are shared

N (still inside):

```bash
echo "written from the mount namespace" > /tmp/nslab-file.txt
```

H:

```bash
cat /tmp/nslab-file.txt
```

**Predict first** before running `cat`.

### Part C — Per-mount flags are per namespace

N:

```bash
mount --bind /tmp /tmp                 # make /tmp its own mount in this namespace
mount -o remount,bind,ro /tmp          # change the PER-MOUNT flag only
touch /tmp/should-fail; echo "exit code in namespace: $?"
```

Do **not** use `mount -o remount,ro /tmp` without `bind`: that also changes the
**super block** read-only flag, which is shared with the host's mount of the
same filesystem.

H:

```bash
touch /tmp/should-succeed && echo "host /tmp still writable"; rm -f /tmp/should-succeed
```

Exit N (`exit`).

### Part D — Unchanged propagation leaks mounts

H:

```bash
findmnt -o TARGET,PROPAGATION /
```

N:

```bash
sudo unshare --mount --propagation unchanged bash
findmnt -o TARGET,PROPAGATION /
mount -t tmpfs leaked-from-ns /mnt/nslab
```

H:

**Predict first.** Will the host see `leaked-from-ns`?

```bash
findmnt /mnt/nslab
sudo mount -t tmpfs leaked-from-host /mnt/nslab
```

N:

```bash
findmnt -R /mnt/nslab
umount -R /mnt/nslab
exit
```

H: `findmnt /mnt/nslab || echo "cleaned"` (run `sudo umount -R /mnt/nslab` if
anything is left).

### Part E — Slave propagation: one direction

N:

```bash
sudo unshare --mount --propagation slave bash
findmnt -o TARGET,PROPAGATION /
mount -t tmpfs from-slave /mnt/nslab
```

H:

```bash
findmnt /mnt/nslab || echo "namespace mount did not leak"
sudo mkdir -p /mnt/nslab-host
sudo mount -t tmpfs from-host /mnt/nslab-host
```

N:

```bash
findmnt /mnt/nslab-host
```

H: `sudo umount /mnt/nslab-host`. N: `findmnt /mnt/nslab-host || echo "unmount propagated too"; exit`.

### Part F — Join a mount namespace

N:

```bash
sudo unshare --mount bash
mount -t tmpfs join-me /mnt/nslab
echo $$
```

H:

```bash
NSPID=<pid printed in N>
findmnt /mnt/nslab || echo "not visible from host"
sudo nsenter --target $NSPID --mount findmnt /mnt/nslab
wc -l < /proc/self/mountinfo
sudo cat /proc/$NSPID/mountinfo | wc -l
```

Exit N. Clean up: `sudo rmdir /mnt/nslab /mnt/nslab-host; rm -f /tmp/nslab-file.txt`.

## Expected observations

**Part A.** The two `mnt` inodes differ. N shows the tmpfs at `/mnt/nslab`; H
shows `not mounted on the host` and an empty directory.

**Part B.** H prints `written from the mount namespace`.

**Part C.** In N, `touch` fails with `Read-only file system`. On H, `/tmp` is
still writable.

**Part D.** H shows `/` as `shared`. N also shows `shared` (the copy is a peer).
H **sees** `leaked-from-ns`; N **sees** `leaked-from-host` stacked on
`/mnt/nslab`. Mount events travel in both directions.

**Part E.** N shows `/` as `private,slave` or `slave`. The namespace's mount does
**not** appear on H. The host's `from-host` mount **does** appear in N, and its
unmount also propagates.

**Part F.** Not visible from H, but visible through `nsenter --mount`. The
`mountinfo` line counts differ by at least one.

## Why this happens

- **A, F.** `unshare(CLONE_NEWNS)` copied the tree; `--propagation private`
  broke the peer relationships, so new mounts stay local. `nsenter` called
  `setns()` into the other tree.
- **B.** The copied `/tmp` mount refers to the same super block; files are not
  part of the mount namespace.
- **C.** `MS_REMOUNT|MS_BIND|MS_RDONLY` sets `MNT_READONLY`, a flag on the
  `struct mount`, and each namespace has its own copies. A remount without
  `MS_BIND` would change the shared super block instead.
- **D.** systemd made `/` shared; copying a shared mount into a new namespace
  makes the copy a peer.
- **E.** `MS_SLAVE` kept the host's peer group as master: host events arrive,
  namespace events do not leave.

## Connection to containers

- Part D is a real bug class: a runtime that forgets to set propagation lets
  container mounts appear on the host.
- Part E is runc's default (`rslave`), and Kubernetes' `HostToContainer`.
- Part B shows why a mount namespace alone is not a container filesystem: the
  runtime must mount a different root and `pivot_root` into it (Chapter 07).
- Part F is how `docker exec` sees the container's files.

## Questions to think about

1. In Part B, how could you prevent the namespace from writing the host's
   `/tmp` using only mount operations inside the namespace?
2. After Part E, the host mounts a new disk at `/data`. Does a container with
   `rslave` propagation see it? What if the container has already done
   `pivot_root` into its own rootfs?
3. Why is `--propagation private` the default of `unshare(1)`, but `rslave`
   the default of runc? What does each choice protect or allow?
4. A multithreaded Java program wants to enter a container's mount namespace
   with `setns()`. Why will it fail, and how do tools such as `nsenter` avoid the
   problem?
