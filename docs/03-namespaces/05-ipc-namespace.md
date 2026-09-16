# 5. IPC Namespace

## 1. The global resource before isolation

Processes that want to share data without files or sockets can use kernel
**inter-process communication (IPC)** objects. Linux has two families whose
objects are identified by **global names**, not by paths or file descriptors:

| Family | Objects | Identified by | API |
|---|---|---|---|
| **System V IPC** | shared memory segments, semaphore sets, message queues | an integer **key** (often from `ftok()`) and an integer ID | `shmget()`, `semget()`, `msgget()`; tools `ipcs`, `ipcmk`, `ipcrm` |
| **POSIX message queues** | message queues | a name such as `/jobs` | `mq_open()`; visible as files in the `mqueue` filesystem |

Because keys and names are global, two unrelated programs that pick the same
key collide, and any process with the right permission bits can attach to
another program's shared memory. System V IPC is old, but still used by
databases (PostgreSQL historically used System V shared memory and still uses
semaphores), Oracle, and some middleware.

The kernel limits these objects with global sysctls such as
`kernel.shmmax`, `kernel.shmall`, `kernel.msgmax`, and `kernel.sem`.

## 2. What the namespace isolates

An IPC namespace contains its own:

- set of System V IPC identifiers (shared memory, semaphores, message queues);
- POSIX message queue filesystem instance;
- values of the IPC sysctls under `/proc/sys/kernel/` (`shm*`, `msg*`, `sem`)
  and `/proc/sys/fs/mqueue/`.

### What it does *not* isolate

**POSIX shared memory** (`shm_open()`) and **POSIX named semaphores** are
implemented as files in the `tmpfs` mounted at `/dev/shm`. They are therefore
isolated by **the mount namespace** (a different `/dev/shm` mount), not by the
IPC namespace. This is a common misconception; the JVM, Chrome, and many
libraries use `/dev/shm`, so container runtimes mount a private tmpfs there
(Chapter 02 §5).

Anonymous IPC such as pipes, socketpairs, and `memfd` is shared through file
descriptors, and is not namespaced at all: it is available only to processes
that inherit or receive the descriptor.

## 3. What changes from the process's perspective

| Operation | In a new IPC namespace |
|---|---|
| `ipcs` / `shmget(key, ...)` | sees only objects created in this namespace; the same key refers to a different object |
| `mq_open("/jobs")` | refers to this namespace's queue, **provided** an `mqueue` filesystem for this namespace is mounted at `/dev/mqueue` |
| IPC sysctls | this namespace's values |

When the last process leaves an IPC namespace, all its System V IPC objects
are destroyed, even segments that were not explicitly removed. This makes IPC
cleanup automatic when a container exits.

## 4. Kernel API

`clone(CLONE_NEWIPC)`, `unshare(CLONE_NEWIPC)`, and `setns(fd, CLONE_NEWIPC)`
all move the caller (or child) into the namespace immediately, like UTS.
Creation requires `CAP_SYS_ADMIN` (in the owning user namespace).

The `mqueue` filesystem instance shows the queues of the IPC namespace of the
process that **mounted** it, a pattern you saw with procfs and PID namespaces.

## 5. Shell experiment

```bash
ipcmk --shmem 1M          # create a System V shared memory segment on the host
ipcs -m
sudo unshare --ipc ipcs -m    # empty
```

## 6. How container runtimes use it

- Each container normally gets its own IPC namespace, and the runtime mounts an
  `mqueue` instance at `/dev/mqueue` and a private tmpfs at `/dev/shm`.
- Containers in the same Kubernetes pod **share** the IPC namespace, so they can
  use System V IPC and POSIX message queues with each other. To share POSIX
  shared memory they also need to share `/dev/shm`, which in Kubernetes is done
  by mounting a common `emptyDir` volume (`medium: Memory`).
- Docker's `--ipc=host` and `--ipc=container:<id>` join an existing IPC
  namespace; `--ipc=shareable` marks a container's namespace as joinable.
- The OCI configuration exposes IPC sysctls in its `sysctl` map; runtimes allow
  them because they are namespaced.

## Evidence

Lab: [`lab-05-ipc-namespace`](../../labs/03-namespaces/lab-05-ipc-namespace/)

## Further Reading

- [`ipc_namespaces(7)`](https://man7.org/linux/man-pages/man7/ipc_namespaces.7.html)
  — the exact list of isolated objects and sysctls.
- [`sysvipc(7)`](https://man7.org/linux/man-pages/man7/sysvipc.7.html) and
  [`mq_overview(7)`](https://man7.org/linux/man-pages/man7/mq_overview.7.html)
  — the two IPC families; `mq_overview` explains the `mqueue` filesystem.
- [`shm_overview(7)`](https://man7.org/linux/man-pages/man7/shm_overview.7.html)
  — shows that POSIX shared memory lives in `/dev/shm`, which explains why the
  IPC namespace does not cover it.
- [`ipcs(1)`](https://man7.org/linux/man-pages/man1/ipcs.1.html),
  [`ipcmk(1)`](https://man7.org/linux/man-pages/man1/ipcmk.1.html) — tools used
  in the lab.
