# Lab 03 — pivot_root: Making the Host Filesystem Unreachable

## Goal

Produce evidence that:

1. `pivot_root` requires a mount namespace, private propagation, and a rootfs
   that is a mount point;
2. after `pivot_root` and a lazy unmount of the old root, the host filesystem is
   **not reachable** from inside, unlike after `chroot`;
3. combining it with PID and other namespaces produces something very close to a
   container.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`unshare`, `pivot_root`, `findmnt`), and
  the rootfs from [Lab 01](../lab-01-build-a-rootfs/) at `/tmp/rootfs`.
- Read: [3. pivot_root](../../../docs/07-rootfs-chroot-pivot-root/03-pivot-root.md).

## Setup

```bash
cp pivot.sh /tmp/rootfs.pivot.sh     # keep a copy outside the rootfs
chmod +x pivot.sh
# Rebuild the rootfs with Lab 01 first if /tmp/rootfs does not exist.
```

## Experiment

### Part A — Why the naive attempt fails

**Predict first.** What error do you expect from `pivot_root` if the rootfs is
not a mount point?

```bash
sudo unshare --mount sh -c '
  mkdir -p /tmp/rootfs/oldroot
  cd /tmp/rootfs
  pivot_root . oldroot; echo "exit code: $?"'
```

### Part B — The correct sequence (as root)

```bash
sudo unshare --mount --pid --fork --uts sh /tmp/rootfs.pivot.sh /tmp/rootfs
```

Inside the resulting shell:

```bash
ls /
cat /proc/mounts 2>/dev/null | wc -l
cat /etc/hostname
ls /oldroot 2>&1 || echo "no /oldroot: host root is gone"
find / -maxdepth 1 -name home -o -maxdepth 1 -name boot 2>/dev/null || echo "host dirs not visible"
exit
```

### Part C — Compare reachability with chroot

chroot leaves the host mounted under the redirect; from the host you can still
see where a chrooted process is rooted (Lab 02 Part D). After `pivot_root` +
lazy unmount, the namespace has no mount for the host at all:

```bash
sudo unshare --mount --pid --fork sh /tmp/rootfs.pivot.sh /tmp/rootfs &
sleep 1
PIVOTED=$(pgrep -n -f '/bin/sh' | tail -n1)
sudo cat /proc/$PIVOTED/mountinfo | wc -l
sudo cat /proc/$PIVOTED/mountinfo | grep -c oldroot || echo "no oldroot mount remains"
sudo kill %1 2>/dev/null
```

### Part D — Rootless pivot_root with a user namespace

```bash
unshare --user --map-root-user --mount --pid --fork --uts sh /tmp/rootfs.pivot.sh /tmp/rootfs
```

Inside:

```bash
id
ls /
cat /proc/self/status | grep -E '^(Uid|CapEff)'
exit
```

## Expected observations

**Part A.** `pivot_root` fails with `Invalid argument` (`EINVAL`): `/tmp/rootfs`
is a plain directory, not a mount point.

**Part B.** `ls /` shows the rootfs contents. `/proc/mounts` has only a handful
of lines (the pivoted root, the self-bind, proc), not the host's dozens.
`cat /etc/hostname` prints `container`. `ls /oldroot` fails or is empty: the old
root was detached. The host directories (`home`, `boot`) are not present.

**Part C.** The pivoted process's `mountinfo` has only a few lines and contains
**no** `oldroot` entry. Contrast with a chrooted process, whose namespace still
holds the entire host mount table.

**Part D.** `id` shows `uid=0(root)` inside the user namespace; `CapEff` is the
full set **within that namespace**; `ls /` shows the rootfs. The whole thing ran
**without sudo**: rootless containers combine exactly these namespaces with
`pivot_root`.

## Why this happens

- **A.** `pivot_root` requires `new_root` to be a mount point (requirement 2 in
  section 3); the script's `mount --bind "$ROOTFS" "$ROOTFS"` satisfies it.
- **B, C.** `mount --make-rprivate /` prevents propagation; `pivot_root` swaps the
  root mount; `umount -l /oldroot` detaches the host root, so no mount in the
  namespace references it, and no path leads to it.
- **D.** A user namespace grants `CAP_SYS_ADMIN` **within it**, which is enough
  for `mount`, `pivot_root`, and `sethostname` on namespaces it owns
  (Chapter 05 §4).

## Connection to containers

- Part B is essentially what runc does in `libcontainer/rootfs_linux.go`
  (`pivotRoot`): make private, self-bind the rootfs, `pivot_root`, mount the
  pseudo-filesystems, detach the old root.
- Part C is the concrete difference between `chroot` and `pivot_root` for
  isolation.
- Part D is the core of a rootless container: no step needed real root.

## Questions to think about

1. Why must propagation be made private/slave *before* `pivot_root`? What would
   happen on a systemd host if you skipped it?
2. After Part B, `/proc/mounts` is short. Which mounts would a full container
   runtime add next, and from which chapter?
3. Why is a **lazy** unmount (`umount -l`) used for the old root instead of a
   normal unmount?
4. In Part D, the same operations needed `sudo` in Parts B–C. Explain, using
   Chapter 05 §4, why the user namespace removes that need.
