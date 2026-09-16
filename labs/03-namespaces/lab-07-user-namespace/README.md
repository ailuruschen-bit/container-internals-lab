# Lab 07 — User Namespace: Mappings and Namespace-Relative Privilege

## Goal

Produce evidence that:

1. an unprivileged user can create a user namespace;
2. before a mapping exists, IDs appear as the overflow UID 65534;
3. `uid_map` translates IDs in both directions, and files keep host IDs;
4. "root" in a user namespace has full capabilities, but only over resources
   owned by that namespace;
5. an unprivileged user can therefore create and use other namespaces through a
   user namespace;
6. an unprivileged user can write a single-line map, but must `deny` setgroups
   before writing `gid_map`.

## Prerequisites

- Linux VM, **run everything as a normal user** unless `sudo` is shown.
- `util-linux` 2.36+ (`unshare --map-root-user`), `libcap2-bin` (`capsh`).
- Read: [7. User namespace](../../../docs/03-namespaces/07-user-namespace.md).

### Check that unprivileged user namespaces are allowed

```bash
cat /proc/sys/user/max_user_namespaces                                     # must be > 0
cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null      # Ubuntu 23.10+
unshare --user --map-root-user bash -c 'grep CapEff /proc/self/status'
```

If `CapEff` is all zeros, or `unshare` fails with `Operation not permitted`,
your distribution restricts them. In a disposable lab VM you can relax the
Ubuntu restriction temporarily:

```bash
sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0   # restore with =1 afterwards
```

## Experiment

### Part A — A user namespace without a mapping

```bash
id
unshare --user bash -c 'id; cat /proc/self/uid_map; echo "(empty map)"; readlink /proc/self/ns/user'
readlink /proc/self/ns/user
```

### Part B — Map root to yourself

```bash
unshare --user --map-root-user bash -c '
  id
  cat /proc/self/uid_map /proc/self/gid_map
  grep CapEff /proc/self/status
  touch /tmp/userns-created.txt
  ls -ln /tmp/userns-created.txt
  ls -ln / | head -n 4'
ls -ln /tmp/userns-created.txt
```

**Predict first.** Who owns `/tmp/userns-created.txt` when viewed from inside
and from outside? Who owns `/` when viewed from inside?

### Part C — Full capabilities, but for what?

```bash
unshare --user --map-root-user bash -c '
  echo "--- read /etc/shadow:";         cat /etc/shadow >/dev/null
  echo "--- change host hostname:";     hostname hacked
  echo "--- load a kernel module:";     modprobe dummy 2>&1 | tail -n1
  echo "--- set the system clock:";     date -s "2001-01-01" >/dev/null
  true'
hostname
```

### Part D — Namespaces owned by the user namespace

```bash
unshare --user --map-root-user --uts --mount --net --ipc bash -c '
  hostname rootless-box && hostname
  mount -t tmpfs mytmp /mnt && findmnt /mnt
  ip link set lo up && ip -brief addr
  ipcmk --shmem 1M >/dev/null && ipcs -m | tail -n +4'
hostname
findmnt /mnt || echo "host /mnt untouched"
```

Try one thing that a user namespace cannot grant:

```bash
unshare --user --map-root-user --mount bash -c 'mount /dev/vda1 /mnt' ; echo "exit code: $?"
```

(Use your root disk device from `findmnt -no SOURCE /`.)

### Part E — PID namespace without sudo

```bash
unshare --user --map-root-user --pid --fork --mount-proc bash -c 'ps -ef; grep NSpid /proc/self/status'
```

Compare with Lab 03, where every PID namespace experiment used `sudo`.

### Part F — Write the maps yourself

Terminal 1:

```bash
unshare --user bash
echo "my pid: $$"
id
```

Terminal 2 (same normal user, **no sudo**):

```bash
P=<pid from terminal 1>
echo "0 100000 1" > /proc/$P/uid_map; echo "map someone else's UID, exit code: $?"
echo "0 $(id -u) 1" > /proc/$P/uid_map; echo "map my own UID, exit code: $?"
echo "0 $(id -u) 1" > /proc/$P/uid_map; echo "second write, exit code: $?"
echo "0 $(id -g) 1" > /proc/$P/gid_map; echo "gid_map exit code: $?"
cat /proc/$P/setgroups
echo deny > /proc/$P/setgroups
echo "0 $(id -g) 1" > /proc/$P/gid_map; echo "gid_map after deny, exit code: $?"
```

Terminal 1:

```bash
id
grep CapEff /proc/self/status
exec bash
grep CapEff /proc/self/status
```

**Predict first.** Why might `CapEff` differ before and after `exec bash`?

### Part G — How the host sees it

While the terminal 1 shell from Part F is still running:

```bash
grep -E '^(Uid|Gid|CapEff)' /proc/$P/status
ps -o pid,user,comm -p $P
lsns -t user
```

## Expected observations

**Part A.** Inside: `uid=65534(nobody) gid=65534(nogroup)`, empty `uid_map`,
and a different `user:[...]` inode than outside.

**Part B.** Inside: `uid=0(root) gid=0(root)`, maps `0 1000 1`, `CapEff`
`000001ffffffffff` (or similar full mask). The new file shows owner `0` inside
and **`1000`** outside. Files under `/` owned by real root show as `65534`
inside.

**Part C.** Every operation fails: `Permission denied` for `/etc/shadow`;
`hostname: you must be root to change the host name` (EPERM); `modprobe` fails
with `Operation not permitted`; `date: cannot set date: Operation not permitted`.
The host's hostname is unchanged.

**Part D.** All four commands **succeed** inside: hostname `rootless-box`,
tmpfs at `/mnt`, `lo` up, one shared memory segment. The host is unaffected.
Mounting the block device fails with `permission denied` or `must be superuser`.

**Part E.** Works without `sudo`: bash is PID 1 and `ps` shows two processes.

**Part F.**
- Mapping UID `100000` fails with `Operation not permitted`: an unprivileged
  writer may map only its own effective UID. (The failed write does not count
  as the one allowed write.)
- Mapping your own UID succeeds; the second write fails (written once).
- The first `gid_map` write fails with `Operation not permitted`.
- `setgroups` shows `allow`; after `deny`, the `gid_map` write succeeds.

In terminal 1, `id` now shows `uid=0(root) gid=0(root)`. `CapEff` is still
`0000000000000000`: this bash was `execve()`d while its UID was the unmapped
65534, so it lost its capabilities at that moment. After `exec bash`, now with
UID 0 inside, `CapEff` is the full set.

**Part G.** The host sees the process as your normal user: `Uid: 1000 1000
1000 1000`. `CapEff` shown from outside is the capability set relative to the
process's own user namespace, which can look "full", but it grants nothing in
the initial namespace. `lsns -t user` lists the new user namespace owned by
your user.

## Why this happens

- **A.** No mapping means no inside representation of the kernel's UID, so
  `overflowuid` is reported.
- **B.** Credentials and inodes store initial-namespace IDs; `uid_map` converts
  at the syscall boundary.
- **C, D.** Capability checks use `ns_capable(owner_user_ns, CAP_...)`. Host
  resources are owned by the initial user namespace; the new namespaces are owned
  by the new user namespace, because the kernel created it first.
- **D (block device).** Mounting a block-device filesystem requires
  `CAP_SYS_ADMIN` in the initial user namespace; only `FS_USERNS_MOUNT`
  filesystems (tmpfs, proc, ...) can be mounted in a user namespace.
- **F.** `new_idmap_permitted()` allows an unprivileged writer to map only its
  own effective ID, and requires `setgroups` to be `deny` for GID maps.

## Connection to containers

- Parts D and E are the foundation of **rootless containers**: everything a
  runtime needs to set up a container, done without real root.
- Part F is what a runtime does internally: the child creates the user
  namespace and waits; the parent writes maps; the child continues.
- Part C is the reason a user namespace makes a container escape much less
  damaging.
- Part B shows the volume ownership problem: files created in the container
  belong to the mapped host UID.

## Questions to think about

1. In Part B, if a container image contains files owned by UID 33 (`www-data`),
   how would they appear inside a user namespace with the single-line map
   `0 1000 1`? What does `/etc/subuid` solve?
2. Why does the kernel create the user namespace before the other namespaces
   when several flags are passed together?
3. Explain why `echo deny > setgroups` is needed for an unprivileged `gid_map`,
   using a file with mode `rwx---rwx`.
4. A default Docker container runs as root without a user namespace. Which of
   the operations in Part C would succeed there, and what stops them instead?
