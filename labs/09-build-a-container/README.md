# Labs — Chapter 09: Build a Container Manually

This chapter's "lab" is the [`minic/`](minic/) program itself: build it, run it,
and add or remove flags to reproduce each stage from the docs.

| Stage | Command | Doc section |
|---|---|---|
| Skeleton | (structure only) | [§1](../../docs/09-build-a-container/01-reexec-skeleton.md) |
| UTS + PID | `sudo ./minic run -rootfs "" -hostname box -- /bin/sh` (note `ps` still shows host) | [§2](../../docs/09-build-a-container/02-uts-pid.md) |
| Mount + rootfs | `sudo ./minic run -hostname box -- /bin/sh` | [§3](../../docs/09-build-a-container/03-mount-rootfs.md) |
| Net + IPC | add `-net` | [§4](../../docs/09-build-a-container/04-net-ipc.md) |
| cgroups | add `-mem $((64*1024*1024)) -pids 64` | [§5](../../docs/09-build-a-container/05-cgroups.md) |
| Caps + seccomp | (always on) verify with `/proc/self/status` | [§6](../../docs/09-build-a-container/06-caps-seccomp.md) |
| Rootless | `./minic run -userns -- /bin/sh` (no sudo) | [§7](../../docs/09-build-a-container/07-finished.md) |

## Setup

```bash
sudo apt-get install -y golang-go busybox-static util-linux
# rootfs:
../07-rootfs-chroot-pivot-root/lab-01-build-a-rootfs/build-busybox-rootfs.sh /tmp/rootfs
# build:
cd minic && go build -o minic . && cd ..
```

See [`minic/README.md`](minic/README.md) for full usage and the list of
deliberate simplifications. The code was verified to build for `linux/amd64` and
`linux/arm64`; the runtime behavior descriptions in the docs are the expected
observations to check against on a Linux VM.
