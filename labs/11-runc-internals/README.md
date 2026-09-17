# Labs — Chapter 11: runc Internals

| Lab | Topic | Needs | Doc section |
|---|---|---|---|
| [lab-01-trace-runc](lab-01-trace-runc/) | strace `runc run`; see namespaces→rootfs→caps→seccomp→execve in order; the created/running split | runc, strace, root | [§2](../../docs/11-runc-internals/02-nsexec.md), [§4](../../docs/11-runc-internals/04-finalize.md) |
| [lab-02-read-rootfs-linux](lab-02-read-rootfs-linux/) | a guided reading of `rootfs_linux.go`, mapping each function to a chapter | the runc source | [§3](../../docs/11-runc-internals/03-init-rootfs.md) |

Setup:

```bash
sudo apt-get install -y runc strace git
# Lab 01 reuses the bundle from Chapter 10 Lab 01 at /tmp/bundle.
# Lab 02 reads source; clone runc and check out the tag matching `runc --version`.
```

These labs are about **observing and reading** a production runtime, not building
one (you already did that in Chapter 09). Trace output ordering was described for
runc v1.2.x; exact syscalls and stage-process names vary by version.
