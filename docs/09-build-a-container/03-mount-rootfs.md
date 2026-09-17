# 3. Mount Namespace and rootfs

## What we add

`CLONE_NEWNS` in the clone flags, and the `setupRootfs` function in `child.go`,
which is the Chapter 07 §4 sequence in Go.

## The sequence, line by line

```go
func setupRootfs(rootfs string) error {
    mountMakeRPrivate()                                   // MS_REC|MS_PRIVATE on /  (Ch. 03 §4)
    syscall.Mount(rootfs, rootfs, "", MS_BIND|MS_REC, "") // rootfs must be a mount point (Ch. 07 §3)
    // mount /proc, /sys (ro), /dev (tmpfs) UNDER the new root:
    mountProc(rootfs + "/proc")                            // needs the PID ns from §2 (Ch. 03 §3)
    syscall.Mount("sysfs", rootfs+"/sys", "sysfs", MS_RDONLY|..., "")
    syscall.Mount("tmpfs", rootfs+"/dev", "tmpfs", ..., "mode=0755"); makeDevNodes(rootfs)
    os.Chdir(rootfs)
    pivotRoot(".", "oldroot")                              // swap the root mount (Ch. 07 §3)
    os.Chdir("/")
    syscall.Unmount("/oldroot", MNT_DETACH)                // detach the host root
    os.Remove("/oldroot")
}
```

Each line ties back to an earlier chapter, which is the point of this whole
chapter: assembling a container is applying what you already know.

Two Go-specific notes:

- `SysProcAttr.Unshareflags = syscall.CLONE_NEWNS` in `parent.go` makes the
  child's mount namespace private up front; combined with `mountMakeRPrivate` in
  the child, mount events never reach the host (Chapter 03 §4).
- `mountProc` runs **after** the PID namespace exists (it was created by the same
  `clone`), so the new procfs reflects the container's PID namespace.

## What changed

Run `sudo ./minic run -hostname box -rootfs /tmp/rootfs -- /bin/sh`:

- `ls /` now shows the **rootfs** contents (BusyBox), not the host.
- `ps` (BusyBox) now shows only **the shell and ps**: the fresh `/proc` reflects
  the PID namespace from §2. The PID isolation that was invisible in §2 is now
  visible.
- `cat /proc/mounts` shows a short list: the pivoted root, proc, sysfs (ro), dev
  — a container-like mount table (compare Chapter 07 Lab 04).
- `/oldroot` is gone: the host filesystem is **absent**, not merely hidden
  (Chapter 07 §3).
- Files created in the container land in the rootfs directory on the host (a
  mount namespace isolates mounts, not files — Chapter 03 §4). Chapter 08's
  OverlayFS is what makes that writable layer private and copy-on-write; `minic`
  uses a plain directory for simplicity.

## What has NOT changed

- **Resources are unbounded**: the container can still use all CPU and memory and
  fork without limit (no cgroup yet — §5).
- **Privilege is intact**: it is still real root with all capabilities and every
  syscall available (no cap drop or seccomp yet — §6).
- **Network** is still the host's (unless `-net`, §4).
- The host still sees the process normally via `/proc/<host-pid>` and
  `/proc/<host-pid>/root` points at the rootfs (Chapter 01 §4).

## Try the failure modes

- Point `-rootfs` at a directory that lacks BusyBox's libraries (if you built a
  dynamic busybox) and watch the "No such file or directory" loader error from
  Chapter 07 Lab 01.
- Remove the `mountMakeRPrivate` call and run on a systemd host: mounts may
  propagate to the host (Chapter 03 §4). Put it back.

## Further Reading

- Chapter 07 [§3 pivot_root](../07-rootfs-chroot-pivot-root/03-pivot-root.md)
  and [§4 assembling a new root](../07-rootfs-chroot-pivot-root/04-assembling-a-new-root.md)
  — the exact sequence `setupRootfs` implements.
- Chapter 03 [§4 mount namespace](../03-namespaces/04-mount-namespace.md).
- runc: [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go)
  — the production version of `setupRootfs` (Chapter 11).
