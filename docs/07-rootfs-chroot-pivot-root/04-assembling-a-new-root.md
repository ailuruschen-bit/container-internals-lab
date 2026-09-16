# 4. Assembling a New Root

Sections 1–3 gave the pieces: a rootfs directory, and `pivot_root` to enter it.
A working container root needs more than the swap: the pseudo-filesystems a
process expects (Chapter 02 §5), sensitive paths hidden, and parts made
read-only. This section is the assembly, in the order a runtime performs it, and
it doubles as the checklist for the mini container in Chapter 09.

## The order and why

Order matters because some mounts depend on namespaces that must exist first,
and because `pivot_root` has requirements (section 3).

```text
1. new namespaces        clone/unshare: mount, PID, UTS, IPC, net, (user, cgroup)   Ch. 03
2. propagation           mount --make-rprivate /  (or rslave)                        Ch. 03 §4
3. rootfs as a mount     mount --bind rootfs rootfs                                  Ch. 02 §3
4. mounts UNDER new root, before pivot:
     /proc               needs the new PID namespace to be useful                    Ch. 02 §5, Ch. 03 §3
     /sys                sysfs, usually read-only                                    Ch. 02 §5
     /dev                tmpfs, mode 0755                                            Ch. 02 §5
       /dev/pts          devpts (new instance)                                       Ch. 02 §5
       /dev/shm          tmpfs, size-limited
       /dev/mqueue       mqueue (needs the IPC namespace)                            Ch. 03 §5
     device nodes        null, zero, full, random, urandom, tty (mknod or bind)      Ch. 02 §5
     symlinks            /dev/fd, /dev/stdin/out/err -> /proc/self/fd
     volumes / config    bind mounts: /etc/hosts, /etc/resolv.conf, user volumes     Ch. 02 §3
5. masked paths          hide dangerous /proc and /sys paths (below)
6. read-only paths       remount specific paths read-only (below)
7. pivot_root            swap and detach the old root                                §3
8. read-only rootfs      if configured, remount / read-only
9. drop caps, seccomp    Ch. 05, Ch. 06
10. set user, execve     Ch. 01 §3, §7
```

Steps 5–8 are the parts not yet covered; the rest are references back to earlier
chapters, which is the point: assembling a root is mostly *applying* what you
have already learned.

## Masked paths

Some files under `/proc` and `/sys` expose host information or allow host impact
even to a confined process. A runtime **masks** them so the container cannot read
or write them. Two techniques:

- **Bind-mount `/dev/null` over a file**, so reads return nothing and writes are
  discarded (used for files);
- **Mount an empty read-only `tmpfs` over a directory**, so its contents vanish
  (used for directories).

The OCI default `maskedPaths` include:

```text
/proc/kcore            /proc/keys            /proc/latency_stats
/proc/timer_list       /proc/sched_debug     /proc/scsi
/sys/firmware          /sys/devices/virtual/powercap        (and more)
```

`/proc/kcore`, for example, is an image of kernel memory; leaving it readable
would be a serious information leak.

## Read-only paths

Other `/proc` paths must remain **visible** (programs read them) but not
**writable** (writing them changes kernel or host state). A runtime makes them
read-only by bind-mounting each path onto itself and remounting with
`MS_RDONLY` (the self-bind trick again, Chapter 02 §3). The OCI default
`readonlyPaths` include:

```text
/proc/asound   /proc/bus   /proc/fs   /proc/irq   /proc/sys   /proc/sysrq-trigger
```

`/proc/sys` is the sysctl tree; making it read-only stops a container from
changing tunables (the ones that are not already namespaced). `/proc/sysrq-trigger`
can reboot or crash the host, so it is masked/read-only.

## Read-only root filesystem

For extra hardening, the whole container root can be remounted read-only after
`pivot_root` (`docker run --read-only`, Kubernetes
`securityContext.readOnlyRootFilesystem: true`). The application then writes only
to explicitly mounted writable volumes or tmpfs (for example an `emptyDir` at
`/tmp`). This limits the damage of a compromise and prevents accidental writes to
the image layer.

## The result

After all steps, the container's mount namespace contains **only** the mounts the
runtime placed, the process's root is the image rootfs, dangerous kernel
interfaces are hidden or read-only, capabilities are dropped, a seccomp filter is
installed, and the application runs as the configured user. Every one of those is
a mechanism from Chapters 01–06 applied through the filesystem operations of
Chapter 02.

## Why this matters for containers

- This section is the container filesystem, top to bottom. When you read
  `/proc/<container-pid>/mountinfo` on a real host, you can now name every line:
  the rootfs, proc/sys/dev/pts/shm/mqueue, the masked `/dev/null` binds, the
  read-only self-binds, and the volume mounts.
- `maskedPaths` and `readonlyPaths` are exactly the OCI fields you will meet in
  Chapter 10, and the code that applies them is in runc (Chapter 11).

## Evidence

Lab: [`lab-04-full-root`](../../labs/07-rootfs-chroot-pivot-root/lab-04-full-root/)

## Further Reading

- OCI Runtime Specification: [config-linux.md — Masked Paths and Readonly Paths](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#masked-paths)
  and [config.md — Root](https://github.com/opencontainers/runtime-spec/blob/main/config.md#root)
  — the exact default lists and the `root.readonly` field (Chapter 10).
- runc source: [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go)
  — `setupRootfs`, `mountToRootfs`, `maskPath`, `readonlyPath`: the assembly in
  code (Chapter 11).
- Docker: [`docker run --read-only`](https://docs.docker.com/reference/cli/docker/container/run/)
  and Kubernetes [Security Context: readOnlyRootFilesystem](https://kubernetes.io/docs/tasks/configure-pod-container/security-context/).
- [`proc(5)`](https://man7.org/linux/man-pages/man5/proc.5.html) — what
  `/proc/kcore`, `/proc/sysrq-trigger`, and the masked/read-only paths actually
  expose, so you understand why each is on the list.
