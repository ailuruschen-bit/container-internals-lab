# 3. The Container init: rootfs

## Where we are

After `nsexec` (§2), the process is in the new namespaces and the Go runtime has
started as the container's init. `libcontainer.StartInitialization()`
(`init_linux.go`) constructs a `linuxStandardInit` and calls its `Init()`
([`standard_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/standard_init_linux.go)).
This section covers the filesystem part of `Init()`; §4 covers privilege and exec.

## The filesystem setup: prepareRootfs / finalizeRootfs

`Init()` calls into
[`rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go),
which is the production version of `minic`'s `setupRootfs` (Chapter 09 §3) and of
Chapter 07 §4. The work is split around the pivot for correctness:

```text
prepareRootfs(pipe, config):                          (before pivot)
    mount("", "/", MS_SLAVE|MS_REC or private)        propagation (Ch. 02 §4, Ch. 03 §4)
    rootfs = prepareRoot(config)                       make the rootfs a mount (bind self)
    for each mount in config.Mounts:                   the OCI mounts array (Ch. 10 §2)
        mountToRootfs(m, rootfs, ...)                  proc, sysfs, tmpfs /dev, devpts,
                                                       bind volumes, cgroup, mqueue (Ch. 02 §5)
    setupDev? createDevices / bind default devices     null, zero, ... (Ch. 02 §5)
    setupPtmx, setupDevSymlinks                        /dev/ptmx, /dev/fd -> /proc/self/fd
    → signal the parent (procReady) so it can run createContainer hooks
    pivotRoot(rootfs)  OR  msMoveRoot + chroot         (Ch. 07 §3)
finalizeRootfs(config):                                (after pivot)
    for readonlyPaths:  bind self + remount ro         (Ch. 07 §4)
    for maskedPaths:    bind /dev/null or ro tmpfs     (Ch. 07 §4)
    remount / read-only if config.Root.Readonly        (Ch. 07 §4)
    set the process working directory
```

Every line has a chapter. Reading `rootfs_linux.go` with this map, and with
Chapter 07 §4 open, is the intended experience: you are checking production code
against a mechanism you already reproduced by hand.

## pivotRoot in runc

runc's `pivotRoot` (in `rootfs_linux.go`) is the careful version of Chapter 07
§3's sequence, with an extra trick to satisfy the "new_root must not be on a
shared mount / must be a mount point" requirements robustly:

```text
pivotRoot(rootfs):
    oldroot = open("/", O_DIRECTORY)          keep a handle to the old root
    newroot = open(rootfs, O_DIRECTORY)
    fchdir(newroot)
    pivot_root(".", ".")                       both args the SAME dir — a known-safe idiom
    fchdir(oldroot)
    mount("", ".", MS_SLAVE|MS_REC)            stop propagation of the unmount
    umount2(".", MNT_DETACH)                   lazily detach the old root (Ch. 07 §3)
    fchdir(newroot)
```

The `pivot_root(".", ".")` form (put_old = new_root) followed by detaching the
old root from the *old* working directory handle is a robust idiom that avoids
needing a separate `oldroot` directory inside the rootfs. It reaches the same end
state as Chapter 07 §3: the container's `/` is the rootfs and the host root is
detached and unreachable.

Where `pivot_root` is impossible (e.g. the root is an initramfs), runc falls back
to `msMoveRoot` + `chroot`, which is why Chapter 07 §3 noted that fallback.

## createDevices, and why not the host /dev

runc mounts a fresh `tmpfs` at `/dev` and either `mknod`s or **bind-mounts** the
allowed devices (Chapter 02 §5), never exposing the host's devtmpfs. The set of
allowed devices comes from `config.linux.devices` plus the OCI default devices.
Access is additionally gated by the **device cgroup**, which on cgroup v2 is an
**eBPF program** (`BPF_CGROUP_DEVICE`) that runc generates and attaches to the
container's cgroup (Chapter 04 §8 noted this). So even if a node exists,
open/read/write of an unlisted device is denied by the eBPF program.

## The role of the sync pipe here

Notice the `procReady` signal in the middle of `prepareRootfs`: runc pauses to
let the **parent** run `createContainer`/`createRuntime` hooks (Chapter 10 §3) at
the correct moment, then resumes to `pivot_root`. This ordered coordination is
exactly what `minic` lacked (Chapter 09 §5) and why some setup (like CNI
networking via hooks) can happen at a precise point relative to the rootfs.

## Why this matters

- `rootfs_linux.go` is the single best file in the whole stack to read after this
  repository, because you have already implemented a simplified version of every
  function in it.
- It shows the production handling of the corner cases `minic` ignored: robust
  pivot_root, device cgroup via eBPF, masked/readonly paths, hook ordering,
  read-only rootfs.

## Evidence

Lab: [`lab-02-read-rootfs-linux`](../../labs/11-runc-internals/lab-02-read-rootfs-linux/)

## Further Reading

- [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go)
  — `prepareRootfs`, `mountToRootfs`, `pivotRoot`, `createDevices`, `maskPath`,
  `readonlyPath`, `finalizeRootfs`.
- [`libcontainer/standard_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/standard_init_linux.go)
  — `Init()`, which calls the above and then §4's steps.
- Chapters 07 §3–§4 and 02 §5 — the mechanisms each function implements.
- Chapter 04 §8 note on the device controller as an eBPF program in cgroup v2.
