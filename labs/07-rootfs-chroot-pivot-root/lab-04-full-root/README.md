# Lab 04 — Assemble a Full Container-Like Root

## Goal

Produce evidence that:

1. a usable container root needs `/proc`, `/dev`, `/sys`, and device nodes, not
   just a pivoted rootfs;
2. `/proc` inside a new PID namespace shows only the container's processes;
3. masked and read-only paths behave as described;
4. the resulting mount table matches what a real container shows.

This lab is the direct precursor to the mini container in Chapter 09; the only
missing pieces are cgroup limits, capability dropping, and seccomp.

## Prerequisites

- Linux VM with `sudo`, `util-linux`, and the rootfs from
  [Lab 01](../lab-01-build-a-rootfs/) at `/tmp/rootfs`.
- Read: [4. Assembling a new root](../../../docs/07-rootfs-chroot-pivot-root/04-assembling-a-new-root.md).

## Setup

```bash
cp enter.sh /tmp/enter.sh && chmod +x /tmp/enter.sh
```

## Experiment

### Part A — Enter the assembled root

```bash
sudo unshare --mount --pid --fork --uts --ipc sh /tmp/enter.sh /tmp/rootfs
```

Inside:

```bash
hostname box && hostname
ps aux 2>/dev/null || busybox ps
ls -l /dev
echo test > /dev/null && echo "/dev/null works"
head -c 8 /dev/urandom | od -An -tx1
cat /proc/mounts
```

**Predict first.** How many processes will `ps` show? Which mounts will
`/proc/mounts` list?

### Part B — Masked and read-only paths

Inside the same shell:

```bash
cat /proc/kcore 2>&1 | head -c 40; echo " <- kcore masked?"
echo 1 > /proc/sys/kernel/printk 2>&1; echo "write /proc/sys exit: $?"
cat /proc/sys/kernel/hostname
```

### Part C — Device isolation

```bash
ls /dev            # only the nodes we created, not host disks
mknod /dev/hostdisk b 8 0 2>&1; echo "mknod block dev exit: $?"
cat /dev/hostdisk 2>&1 | head -c 20; echo " <- can we read a disk we invented?"
rm -f /dev/hostdisk
exit
```

### Part D — Compare with a real container's mounts

If you have Docker:

```bash
CID=$(docker run -d alpine sleep 300)
PID=$(docker inspect -f '{{.State.Pid}}' "$CID")
sudo cat /proc/$PID/mountinfo | awk '{print $5, $9}' | sort -u
docker rm -f "$CID"
```

Compare the mount points with what your `enter.sh` produced.

## Expected observations

**Part A.** `hostname` becomes `box`. `ps` shows only **2–3** processes (the
shell as PID 1, maybe `ps`), because `/proc` was mounted inside the new PID
namespace. `/dev` contains `null`, `zero`, `random`, `urandom`, `tty`, `pts`,
`shm`. `/dev/null` and `/dev/urandom` work. `/proc/mounts` lists roughly:
the pivoted root, `proc`, `sysfs (ro)`, `dev (tmpfs)`, `devpts`, `shm`, and the
`/proc/sys` read-only self-bind — a handful of lines, like a container.

**Part B.** `cat /proc/kcore` returns nothing (masked by `/dev/null`). Writing
`/proc/sys/kernel/printk` fails with `Read-only file system`. Reading
`/proc/sys/kernel/hostname` still works (read-only, not hidden) and shows `box`.

**Part C.** `ls /dev` shows only the created nodes, no `sda`/`vda`. Creating a
block node may **succeed** (if run as root with `CAP_MKNOD` and `/dev` is not
`nodev`), but reading it typically fails or returns nothing here because the
tmpfs and, in a real runtime, the device cgroup and `nodev` would block it. In a
user-namespace run, `mknod` of a real device fails outright (Chapter 05 §4).

**Part D.** The real container's mount points are essentially the same set:
`/`, `/proc`, `/sys`, `/dev`, `/dev/pts`, `/dev/shm`, `/dev/mqueue`,
`/etc/resolv.conf`, `/etc/hostname`, `/etc/hosts`, plus masked `/proc/*` and
read-only `/proc/*` entries.

## Why this happens

- **A.** `enter.sh` mounted `/proc` after the PID namespace existed, so it shows
  that namespace (Chapter 03 §3). The short mount list is the pivoted, private
  namespace (section 3).
- **B.** The `/dev/null` bind hides `kcore`; the `/proc/sys` self-bind + remount
  makes it read-only while still visible.
- **C.** `/dev` is a fresh tmpfs with only the nodes created; the host's disks
  were never placed there, and (in a full runtime) the device cgroup and `nodev`
  prevent using any node for a real device.

## Connection to containers

- Part D is the proof that your hand-built root matches a production container's
  filesystem. The remaining differences (cgroups, capabilities, seccomp) are
  Chapters 04–06, assembled in Chapter 09.
- Parts B and C are the OCI `maskedPaths`, `readonlyPaths`, and device handling.

## Questions to think about

1. Why does `ps` show only a few processes here, but showed all host processes in
   Lab 02 Part B? What is the one difference?
2. If you removed `--pid --fork` from the `unshare` line, what would `ps` show,
   and why? (Chapter 03 §3.)
3. Which two mechanisms, not in this lab, would ensure a container root truly
   cannot use a block device even if it can `mknod` one? (Chapters 02 §5, 04.)
4. List every mount in Part A and name the chapter that introduced it.
