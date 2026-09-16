# Lab 05 — IPC Namespace: System V IPC, POSIX Message Queues, and /dev/shm

## Goal

Produce evidence that:

1. System V IPC objects are per IPC namespace and are destroyed with it;
2. POSIX message queues are per IPC namespace, but only visible through an
   `mqueue` mount made for that namespace;
3. IPC sysctls have per-namespace values;
4. POSIX shared memory in `/dev/shm` is **not** isolated by the IPC namespace.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`ipcs`, `ipcmk`, `ipcrm`, `unshare`).
- Read: [5. IPC namespace](../../../docs/03-namespaces/05-ipc-namespace.md).

## Experiment

### Part A — System V shared memory

```bash
ipcmk --shmem 1M
ipcs -m
```

**Predict first.** What will `ipcs -m` show in a new IPC namespace?

```bash
sudo unshare --ipc ipcs -m
```

### Part B — Objects created inside die with the namespace

```bash
sudo unshare --ipc bash -c 'ipcmk --shmem 2M; ipcmk --queue; ipcs -m -q'
ipcs -m -q
```

Remove the host segment from Part A: `ipcrm --shmem-id <id>` (use the ID shown
by `ipcs -m`).

### Part C — POSIX message queues need their own mqueue mount

```bash
findmnt /dev/mqueue || sudo mount -t mqueue mqueue /dev/mqueue
sudo touch /dev/mqueue/host-queue            # creating a file creates a queue
cat /dev/mqueue/host-queue
```

**Predict first.** In a new IPC namespace *without* a new mount, will
`/dev/mqueue/host-queue` still be listed?

```bash
sudo unshare --ipc ls /dev/mqueue
sudo unshare --ipc --mount bash -c 'mount -t mqueue mqueue /dev/mqueue; ls -la /dev/mqueue; touch /dev/mqueue/ns-queue; ls /dev/mqueue'
ls /dev/mqueue
sudo rm /dev/mqueue/host-queue
```

### Part D — Per-namespace IPC sysctls

```bash
cat /proc/sys/kernel/msgmax
sudo unshare --ipc bash -c 'echo 16384 > /proc/sys/kernel/msgmax; cat /proc/sys/kernel/msgmax'
cat /proc/sys/kernel/msgmax
```

### Part E — `/dev/shm` is a mount, not an IPC object

```bash
echo "posix shm data" > /dev/shm/lab-shm
sudo unshare --ipc cat /dev/shm/lab-shm
sudo unshare --ipc --mount bash -c 'mount -t tmpfs -o size=16m shm /dev/shm; ls /dev/shm'
rm /dev/shm/lab-shm
```

## Expected observations

**Part A.** The host shows one segment of 1048576 bytes; the new namespace shows
an empty table.

**Part B.** Inside: one 2 MB segment and one message queue. After the command
ends, the host's `ipcs` does not show them: the namespace, and its objects,
were destroyed.

**Part C.** `cat` on the queue file prints a line such as
`QSIZE:0 NOTIFY:0 SIGNO:0 NOTIFY_PID:0`. With only `--ipc`, `ls /dev/mqueue`
**still shows `host-queue`**, because `/dev/mqueue` is still the host's mount,
which belongs to the host's IPC namespace. With a new mount of `mqueue`, the
listing is empty, then shows only `ns-queue`. The host never sees `ns-queue`.

**Part D.** Inside, `msgmax` becomes `16384`; on the host, it is unchanged
(typically `8192`).

**Part E.** `cat` in the new IPC namespace **prints the host's data**. Only a new
tmpfs mount at `/dev/shm` (in a mount namespace) hides it.

## Why this happens

- **A, B.** System V IPC IDs live in `struct ipc_namespace`; when its last user
  exits, `free_ipcs()` destroys all remaining objects.
- **C.** An `mqueue` superblock is bound to the IPC namespace of the mounter;
  the path `/dev/mqueue` is resolved through the mount namespace.
- **D.** These sysctl files read and write fields of the caller's
  `ipc_namespace`.
- **E.** `shm_open()` is `open()` on a file in `/dev/shm`; only path resolution
  (mount namespace) decides which file that is.

## Connection to containers

- Parts C and E show that a runtime must combine the IPC namespace with a mount
  namespace and fresh `mqueue` and `tmpfs` mounts to isolate IPC completely.
- Part B is why an exited container cannot leak System V shared memory on the
  host.
- Part D explains why container engines allow `--sysctl kernel.msgmax=...`
  per container but reject most `kernel.*` sysctls: only the namespaced ones are
  safe.

## Questions to think about

1. Two containers in the same pod share an IPC namespace but have separate
   mount namespaces. Can they share a POSIX shared memory segment created with
   `shm_open()`? What would you add to make it work?
2. Why do you think the IPC namespace destroys System V objects automatically,
   while the host kernel keeps them until `ipcrm` or reboot?
3. Which isolation is used when a JVM in a container uses
   `-XX:+UseLargePages` with System V shared memory (`shmget` with
   `SHM_HUGETLB`)?
