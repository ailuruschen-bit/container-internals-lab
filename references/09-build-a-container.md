# References — Chapter 09: Build a Container Manually

This chapter synthesizes Chapters 01–08; its references are mostly those
chapters plus the Go and runtime sources that mirror `minic`.

## Go standard library (primary for the code)

- [`syscall.SysProcAttr`](https://pkg.go.dev/syscall#SysProcAttr) — `Cloneflags`,
  `Unshareflags`, `UidMappings`, `GidMappings` (§1, §2, §7).
- [`os/exec`](https://pkg.go.dev/os/exec) — the re-exec of `/proc/self/exe` (§1).
- [`syscall`](https://pkg.go.dev/syscall) on Linux — `Mount`, `PivotRoot`,
  `Sethostname`, `Mknod`, `Exec`, `SockFilter`, `SockFprog` (§3, §6).
- [`src/syscall/exec_linux.go`](https://github.com/golang/go/blob/master/src/syscall/exec_linux.go)
  — how Go writes the uid/gid maps and issues the raw `clone` (§7).

## Kernel interfaces used (primary)

- [`clone(2)`](https://man7.org/linux/man-pages/man2/clone.2.html) — the
  `CLONE_NEW*` flags (§2, §4, §7).
- [`pivot_root(2)`](https://man7.org/linux/man-pages/man2/pivot_root.2.html),
  [`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html) (§3).
- [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) —
  `PR_CAPBSET_DROP`, `PR_SET_NO_NEW_PRIVS`, `PR_SET_SECCOMP` (§6).
- [Seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html) — to
  validate the hand-built filter in `seccomp.go` (§6).
- [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html) — the
  files `cgroups.go` writes (§5).

## Production runtime sources (the "done properly" versions)

- runc: [`libcontainer/nsenter`](https://github.com/opencontainers/runc/tree/main/libcontainer/nsenter)
  (the C bootstrap that replaces re-exec, §1),
  [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go)
  (§3),
  [`libcontainer/standard_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/standard_init_linux.go)
  (the order of caps/seccomp/exec, §6). Chapter 11 reads these.
- [opencontainers/cgroups](https://github.com/opencontainers/cgroups) — the
  cgroup manager (§5).
- [seccomp/libseccomp-golang](https://github.com/seccomp/libseccomp-golang) — the
  allow-list profile compiler `minic` deliberately avoids (§6).

## Secondary

- Liz Rice, ["Containers From Scratch"](https://www.youtube.com/watch?v=8fi7uSYlOdc)
  (GOTO 2018) and the companion repo — a second Go implementation to compare with
  `minic`.
- Chapters 01–08 of this repository: every mechanism `minic` uses is explained
  there, and `minic`'s comments cite the exact sections.
