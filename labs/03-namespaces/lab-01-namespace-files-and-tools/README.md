# Lab 01 — Namespace Files, lsns, and Namespace Lifetime

## Goal

Produce evidence that:

1. every process is a member of one namespace of each type, identified by an
   inode number;
2. `unshare` changes the namespace inode for exactly the types requested;
3. `nsenter` joins an existing namespace through its `/proc/<pid>/ns` file;
4. a namespace outlives all its processes if a bind mount of its file exists,
   and disappears when the last reference is removed.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`unshare`, `nsenter`, `lsns`).
- Read: [1. What a namespace is](../../../docs/03-namespaces/01-namespace-concepts.md).

## Background

`readlink /proc/<pid>/ns/<type>` prints `type:[inode]`. Equal inodes mean the
same namespace. `lsns` groups all processes by namespace.

## Experiment

### Part A — The initial namespaces

```bash
ls -l /proc/$$/ns
sudo lsns
sudo lsns --type uts
```

**Predict first.** On a VM without containers, how many distinct UTS
namespaces will `lsns --type uts` list?

### Part B — `unshare` creates only what you ask for

In a **second terminal**:

```bash
sudo unshare --uts bash
echo "PID in new UTS ns: $$"
readlink /proc/$$/ns/uts /proc/$$/ns/net /proc/$$/ns/mnt
hostname lab-uts
hostname
```

In the **first terminal**:

```bash
hostname
readlink /proc/$$/ns/uts /proc/$$/ns/net /proc/$$/ns/mnt
sudo lsns --type uts
```

### Part C — Join it with `nsenter`

Use the PID printed in the second terminal:

```bash
PID=<pid from second terminal>
sudo nsenter --target $PID --uts hostname
sudo nsenter --uts=/proc/$PID/ns/uts bash -c 'hostname; readlink /proc/self/ns/uts'
```

### Part D — Keep a namespace alive without processes

In the first terminal, pin the namespace with a bind mount:

```bash
sudo touch /tmp/pinned-uts
sudo mount --bind /proc/$PID/ns/uts /tmp/pinned-uts
findmnt /tmp/pinned-uts
```

Now **exit** the shell in the second terminal (`exit`). Then, in the first
terminal:

```bash
ps -p $PID || echo "the process is gone"
sudo lsns --type uts                                  # does it still list lab-uts?
sudo nsenter --uts=/tmp/pinned-uts hostname
```

**Predict first.** Will `nsenter` still print `lab-uts`?

Release the last reference:

```bash
sudo umount /tmp/pinned-uts
sudo nsenter --uts=/tmp/pinned-uts hostname; echo "exit code: $?"
sudo rm /tmp/pinned-uts
```

### Part E — `ip netns` uses the same trick

```bash
sudo ip netns add lab-blue
findmnt -o TARGET,SOURCE,FSTYPE /run/netns/lab-blue
ls -l /run/netns/
sudo ip netns exec lab-blue readlink /proc/self/ns/net
readlink /proc/self/ns/net
sudo ip netns delete lab-blue
```

## Expected observations

**Part A.** All links show numbers around `4026531834`–`4026531841`.
`lsns --type uts` lists **one** namespace (`NPROCS` equal to nearly all
processes), unless some service already uses its own (for example
`systemd-hostnamed`, `systemd-timesyncd`, or snap applications on some
distributions).

**Part B.** Second terminal: the `uts` inode differs from the first terminal's;
`net` and `mnt` inodes are the **same**. `hostname` shows `lab-uts` in the
second terminal and the original name in the first. `lsns` now lists an extra
UTS namespace containing `bash`.

**Part C.** Both commands print `lab-uts`, and `readlink` shows the new UTS
inode.

**Part D.** After the shell exits, `ps` reports that the process is gone.
`lsns` does **not** list the namespace (it only finds namespaces through
processes), yet `nsenter --uts=/tmp/pinned-uts hostname` still prints
`lab-uts`. After `umount`, `nsenter` fails (the file is now an ordinary empty
file, not a namespace), with an error such as
`reassociate to namespace 'ns/uts' failed: Invalid argument`.

**Part E.** `findmnt` shows `/run/netns/lab-blue` with filesystem type `nsfs`.
The `net` inode inside `ip netns exec` differs from the host's.

## Why this happens

- **A–C.** Each namespace is a kernel object exposed as an `nsfs` inode. `unshare`
  called `unshare(CLONE_NEWUTS)` and then `execve("bash")`. `nsenter` opened the
  namespace file and called `setns(fd, CLONE_NEWUTS)` before `execve()`.
- **D.** The bind mount holds a reference to the `nsfs` inode, which holds the
  namespace. Unmounting drops the last reference, and the kernel frees the
  namespace.
- **E.** `ip netns add` creates a network namespace and bind-mounts its file
  under `/run/netns/`; `ip netns exec` calls `setns()` on that file.

## Connection to containers

- `docker exec` and `kubectl exec` are `nsenter` in spirit: open the container
  process's namespace files, `setns()`, `execve()`.
- Pinning namespaces with bind mounts is how container network setups keep a
  pod's network namespace alive independently of any single container process.
- `lsns` on a host with containers shows one group of namespaces per container
  (or per pod), which is often the quickest way to find a container's PID from
  the host.

## Questions to think about

1. Why does `lsns` not show the pinned namespace in Part D? What would a tool
   need to scan to find all namespaces on a system?
2. In Part B, the new shell shares the host's network namespace. What would
   `ip addr` show inside it?
3. Why does `nsenter` need `sudo` even when joining a UTS namespace that your own
   user created with `sudo unshare`?
4. How would you find which container a given host PID belongs to, using only
   `/proc` and `lsns`?
