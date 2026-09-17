# minic — a learning mini container runtime

`minic` is a small container runtime in pure Go (standard library only). It is
the working artifact for Chapter 09. It is **for learning, not production**; runc
(Chapter 11) is the real thing.

## Build

On Linux:

```bash
go build -o minic .
```

Or cross-compile from another OS:

```bash
GOOS=linux GOARCH=amd64 go build -o minic .   # or GOARCH=arm64
```

## Prepare a rootfs

Use the BusyBox rootfs from Chapter 07 Lab 01:

```bash
../../07-rootfs-chroot-pivot-root/lab-01-build-a-rootfs/build-busybox-rootfs.sh /tmp/rootfs
```

## Run

```bash
sudo ./minic run -hostname box -mem $((64*1024*1024)) -pids 64 -- /bin/sh
```

Inside the shell, confirm the isolation:

```sh
hostname                     # box
echo $$                      # 1  (PID namespace)
ps                           # only the shell and ps (own /proc)
ls /                         # the rootfs, not the host
cat /sys/fs/cgroup/... 2>/dev/null; cat /proc/self/status | grep -E 'CapBnd|NoNewPrivs|Seccomp'
mount 2>/dev/null; echo "mount blocked: $?"   # seccomp blocks mount → EPERM
```

Rootless (no sudo), on a host that allows unprivileged user namespaces:

```bash
./minic run -userns -hostname box -- /bin/sh
```

## Files

| File | Chapter mapping |
|---|---|
| `main.go` | dispatch: `run` (parent) vs `child` (re-exec) — §1 |
| `config.go` | flags and the child's environment — §1 |
| `parent.go` | clone flags, re-exec, user-namespace maps, cgroup — §2, §4, §5, §7 |
| `child.go` | sethostname, rootfs + pivot_root, /proc, then execve — §2, §3 |
| `cgroups.go` | cgroup v2 create, limits, membership — §5 (Ch. 04) |
| `caps.go` | drop bounding-set capabilities, no_new_privs — §6 (Ch. 05) |
| `seccomp.go` | a classic-BPF deny-list seccomp filter — §6 (Ch. 06) |

## What minic does and does NOT do

**Does:** UTS/PID/mount/IPC (and optional net, user) namespaces; pivot_root into
a rootfs with proc/dev/sys; a cgroup with `memory.max`/`pids.max`; drops a set of
dangerous capabilities; `no_new_privs`; a small seccomp filter.

**Does not (on purpose, to stay readable):** pull or unpack images (use a
prepared rootfs); set up veth/bridge networking (the net namespace is isolated
with only `lo`); a full OCI-compliant capability rewrite via `capset`; an
allow-list seccomp profile; a parent/child sync pipe for strict setup ordering;
cgroup delegation for rootless. Chapters 10–11 show how runc does these properly.

See the Chapter 09 docs for a section-by-section walkthrough.
