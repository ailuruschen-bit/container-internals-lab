# Lab 01 — Build a Root Filesystem by Hand

## Goal

Produce evidence that:

1. a rootfs is an ordinary directory tree, not an image format;
2. a dynamically linked program needs its loader and libraries present in the
   rootfs, or it fails to start;
3. a static binary needs almost nothing else;
4. an OCI/Docker image's root filesystem is the same kind of tree.

## Prerequisites

- Linux VM with `sudo`, `busybox-static` (or `busybox`), `coreutils`.
  Optional: `podman` or `docker` for Part D.
- Read: [1. What a root filesystem is](../../../docs/07-rootfs-chroot-pivot-root/01-what-is-a-rootfs.md).

## Experiment

### Part A — Build it

```bash
sudo apt-get install -y busybox-static      # Debian/Ubuntu
chmod +x build-busybox-rootfs.sh
./build-busybox-rootfs.sh /tmp/rootfs
ls -la /tmp/rootfs
file /tmp/rootfs/bin/busybox
```

### Part B — The missing-library failure

Copy a **dynamic** program into the rootfs without its libraries, then try to run
it (Part D of the next lab actually enters the rootfs; here we simulate the
lookup failure with `chroot`, which needs root):

```bash
cp /bin/ls /tmp/rootfs/bin/ls-dynamic 2>/dev/null || cp "$(command -v ls)" /tmp/rootfs/bin/ls-dynamic
ldd "$(command -v ls)"
sudo chroot /tmp/rootfs /bin/ls-dynamic /; echo "exit code: $?"
```

**Predict first.** `ls` exists in the rootfs. Will it run? What error, if any?

### Part C — The static program works

```bash
sudo chroot /tmp/rootfs /bin/busybox ls -l /
sudo chroot /tmp/rootfs /bin/sh -c 'echo "hello from inside"; cat /etc/hostname; ls /'
```

### Part D — An image is the same kind of tree

With Podman (no daemon) or Docker:

```bash
mkdir -p /tmp/alpine-rootfs
podman export "$(podman create alpine)" 2>/dev/null | tar -x -C /tmp/alpine-rootfs 2>/dev/null || \
docker export "$(docker create alpine)" | tar -x -C /tmp/alpine-rootfs
ls /tmp/alpine-rootfs
find /tmp/alpine-rootfs -maxdepth 1 -type d | sort
sudo chroot /tmp/alpine-rootfs /bin/sh -c 'cat /etc/os-release | head -n1; ls /bin | head'
```

### Cleanup

```bash
sudo rm -rf /tmp/rootfs /tmp/alpine-rootfs
```

## Expected observations

**Part A.** `/tmp/rootfs` contains `bin`, `etc`, `proc`, `sys`, `dev`, `tmp`,
etc. `bin` is full of symlinks to `busybox`. `file` reports the busybox binary as
`statically linked` (with `busybox-static`) or `dynamically linked` otherwise.
Size is a few MB.

**Part B.** If your system `ls` is dynamically linked (typical),
`chroot ... /bin/ls-dynamic` fails with
`chroot: failed to run command '/bin/ls-dynamic': No such file or directory` —
even though the file exists. The "No such file" refers to the missing dynamic
loader inside the rootfs.

**Part C.** BusyBox `ls` and `sh` work: they print the directory listing,
`hello from inside`, and `container`. If busybox is static, no libraries were
needed; if dynamic, the build script copied them.

**Part D.** The exported image expands to a normal tree (`bin`, `etc`, `lib`,
`usr`, ...). `chroot` into it runs Alpine's `sh` and prints its `os-release`.

## Why this happens

- **B.** `execve()` of a dynamic binary makes the kernel load the interpreter
  named in the ELF header (`/lib64/ld-linux-*.so.2`). Path resolution for that
  interpreter happens **inside the new root**, where it is absent, so the kernel
  returns `ENOENT`, which the shell reports as "No such file or directory".
- **C.** A static binary needs no interpreter or libraries.
- **D.** Image layers are tar archives of directory trees; exporting flattens
  them into one tree, which is exactly a rootfs.

## Connection to containers

- This lab builds the artifact that Chapters 03 (mount namespace), 07
  (`pivot_root`), and 09 (mini container) place a process into.
- Part B is the single most common "it works on my machine but not in the
  container" cause: a binary copied into a minimal image without its libraries.
- Part D shows that "pulling an image" ultimately produces a directory tree like
  the one you built by hand.

## Questions to think about

1. Why does a Go program usually run in a `FROM scratch` image with no libraries,
   while a Python or Java program does not?
2. If `chroot ... /bin/ls-dynamic` fails with "No such file or directory", how
   would you find *which* file is actually missing?
3. An image runs as UID 1000 but the files were unpacked as UID 0. What happens
   when the app tries to write to its own `/app` directory, and how do runtimes
   avoid it?
