# Lab 02 — chroot, Its Correct Use, and Its Escape

## Goal

Produce evidence that:

1. `chroot()` redirects absolute path resolution to a new root;
2. it does not change the working directory, and does not isolate processes,
   mounts, or credentials;
3. a privileged process can escape a naive `chroot`;
4. a chrooted root process still sees host processes and the host mount table.

## Prerequisites

- Linux VM with `sudo`, `gcc`, the rootfs from
  [Lab 01](../lab-01-build-a-rootfs/) at `/tmp/rootfs`.
- Read: [2. chroot](../../../docs/07-rootfs-chroot-pivot-root/02-chroot.md).

**Safety:** the escape is harmless (it only lists the real `/`), but run it in a
disposable VM.

## Experiment

### Part A — Correct use

```bash
sudo chroot /tmp/rootfs /bin/sh -c 'pwd; ls /; cat /etc/hostname'
```

`chroot(8)` does the `chdir("/")` for you, so `pwd` is `/`.

### Part B — chroot does not isolate processes or mounts

```bash
sudo chroot /tmp/rootfs /bin/sh -c '
  mount 2>/dev/null | head -n 3 || cat /proc/mounts 2>/dev/null | head -n 3 || echo "(no /proc mounted yet)"
  /bin/busybox ps 2>/dev/null | head || echo "(no /proc, so ps sees nothing)"'
```

Now mount the host's /proc into the jail and look again:

```bash
sudo mount -t proc proc /tmp/rootfs/proc
sudo chroot /tmp/rootfs /bin/busybox ps aux 2>/dev/null | head
sudo chroot /tmp/rootfs /bin/busybox ps aux 2>/dev/null | wc -l
sudo umount /tmp/rootfs/proc
```

**Predict first.** With the host's /proc mounted, will `ps` inside the chroot
show only jail processes, or host processes too?

### Part C — The escape

```bash
gcc -Wall -o chroot_escape chroot_escape.c
cp chroot_escape /tmp/rootfs/          # so it exists inside the jail path
sudo /tmp/rootfs/chroot_escape /tmp/rootfs 2>&1 | head -n 20
```

If the jail has no host-compatible `/bin/ls`, the escape still happens; only the
final listing may fail. Confirm the escape a second way:

```bash
sudo chroot /tmp/rootfs /chroot_escape /tmp/rootfs 2>&1 | head -n 20
rm -f /tmp/rootfs/chroot_escape
```

**Predict first.** Will the listing show the jail's files (just `bin etc proc
...`) or the host's root (`boot home root usr var ...`)?

### Part D — A chrooted process is still a normal host process

In one terminal:

```bash
sudo chroot /tmp/rootfs /bin/sh -c 'echo "jail shell pid inside: $$"; sleep 300'
```

In another terminal:

```bash
JAIL=$(pgrep -n sleep)
sudo readlink /proc/$JAIL/root
ps -o pid,comm -p $JAIL
sudo ls /proc/$JAIL/root/
```

## Expected observations

**Part A.** `pwd` is `/`, `ls /` shows the rootfs directories, `cat` prints
`container`.

**Part B.** Without `/proc`, `ps` shows nothing (BusyBox `ps` needs `/proc`).
After mounting the host's `/proc`, `ps aux` shows **host** processes: dozens or
hundreds of lines. chroot did not give a PID namespace.

**Part C.** The listing shows the **host** root filesystem (`boot`, `home`,
`usr`, `var`, ...), not the jail's small set. The escape succeeded.

**Part D.** `readlink /proc/$JAIL/root` prints `/tmp/rootfs`: the host can see
exactly where the jail is. `ps` shows the process normally. `ls
/proc/$JAIL/root/` lists the jail contents from outside.

## Why this happens

- **A.** `chroot(8)` sets the root and `chdir`s into it.
- **B.** chroot changes only `fs->root`. Processes are visible because there is no
  PID namespace and `/proc` (once mounted) reflects the host's PID namespace
  (Chapter 03 §3).
- **C.** The program never `chdir`ed into the jail, so its cwd stayed above the
  new root; `..` climbed to the real root, and `chroot(".")` re-rooted there.
- **D.** The process's mount namespace is the host's, so `/proc/<pid>/root` (a
  magic link, Chapter 01 §4) resolves to the jail path and the host can browse
  it.

## Connection to containers

- This lab is the evidence behind "a container is not just chroot". Each
  weakness maps to a mechanism a real runtime adds: PID namespace (Part B),
  dropped capabilities/seccomp to prevent re-mount and re-chroot (Part C), mount
  namespace + `pivot_root` to make the old root unreachable (Part C, D), user
  namespace so the escapee is unprivileged.
- Part D (`/proc/<pid>/root`) is a genuinely useful debugging tool for real
  containers, as noted in Chapter 01 §4.

## Questions to think about

1. Exactly which line in `chroot_escape.c` would you change to make the escape
   fail, and why does `chroot(8)` include that step?
2. Even with `chdir("/")`, a privileged process can still escape. Name two
   capabilities that enable an escape and how.
3. Why does `pivot_root` (next section) prevent the Part C style of escape in a
   way `chroot` cannot?
