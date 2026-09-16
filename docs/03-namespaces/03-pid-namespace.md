# 3. PID Namespace

## 1. The global resource before isolation

Chapter 01 §2 described the process ID space: every process has a PID, PIDs are
unique among live processes, and every process tree leads to PID 1. Without
namespaces there is **one** such space per machine, with these consequences:

- any process can see every other process in `/proc` and `ps`;
- any process can *attempt* to send a signal to any PID (permission checks then
  decide);
- a program cannot be PID 1 unless it is the machine's init;
- restoring a checkpointed process tree on another machine is hard, because the
  original PIDs may already be in use.

## 2. What the namespace isolates

A PID namespace provides an **independent PID number space**. The first process
created in a new PID namespace gets **PID 1** in it; the next gets 2, and so on.

PID namespaces are **nested**. Each new PID namespace has a parent, the PID
namespace of the process that created it, and a process is visible in its own
namespace **and in every ancestor namespace**, with a different PID at each
level:

```text
initial PID namespace (level 0)        what the host sees
 ├── 1  systemd
 ├── 812 containerd-shim
 │    └── 4242  nginx master  ─────────┐
 │         ├── 4250 nginx worker ───┐  │
 │         └── 4251 nginx worker ─┐ │  │
 │                                │ │  │   same processes, second numbering
 └── child PID namespace (level 1)│ │  │
      ├── 1  nginx master  ◄──────┼─┼──┘
      ├── 7  nginx worker  ◄──────┼─┘
      └── 8  nginx worker  ◄──────┘
```

A process in the child namespace can see and signal only processes in its own
namespace and its descendants. It **cannot see processes of the parent
namespace at all**; they have no PID in its numbering.

In the kernel, a process's identity is a `struct pid` that holds an array of
`(namespace, number)` pairs, one per level. `/proc/<pid>/status` shows the
numbers:

```text
NSpid:  4242    1          ← PID in each namespace, from the outermost to the process's own
```

## 3. What changes from the process's perspective

| Operation | Result inside the child PID namespace |
|---|---|
| `getpid()` | the PID in the process's **own** namespace (for example 1) |
| `getppid()` | the parent's PID in the process's namespace; **0** if the parent is in an ancestor namespace |
| `kill(pid, sig)` | `pid` is interpreted in the caller's namespace; processes outside are unreachable |
| `/proc` | shows the processes of the PID namespace **that the procfs instance was mounted for** (see below) |
| Orphans | reparented to PID 1 **of this namespace** (or a subreaper in it) |

### PID 1 inside a PID namespace

Everything Chapter 01 said about PID 1 now applies to the **first process of
each PID namespace**, called the namespace's **init process**:

1. **Reaping.** Orphaned processes in the namespace are reparented to it, so it
   must reap zombies.
2. **Signal protection.** Processes **in the same namespace** can send it only
   signals for which it has installed a handler. Processes in an **ancestor**
   namespace are subject to the same rule, with one exception: `SIGKILL` and
   `SIGSTOP` from an ancestor are always delivered. That is how the host can
   always kill a container.
3. **Its death ends the namespace.** When the init process of a PID namespace
   terminates, the kernel sends `SIGKILL` to **every other process in the
   namespace** (and in its descendant namespaces). After that, `fork()` in that
   namespace fails with `ENOMEM`. The namespace is destroyed once all processes
   are reaped.

Rule 3 finally explains the last step of the container stop sequence from
Chapter 01 §6: killing the container's PID 1 kills the rest of the container.

## 4. Kernel API

### `clone(CLONE_NEWPID)`

The child is created in a new PID namespace and becomes its PID 1. This is the
most direct way.

### `unshare(CLONE_NEWPID)` — the surprising one

A process's PID cannot change: many kernel and user-space structures depend on
it. Therefore `unshare(CLONE_NEWPID)` does **not** move the caller into the new
namespace. It changes only `nsproxy->pid_ns_for_children`. The **next child**
the caller creates becomes PID 1 of the new namespace.

```text
unshare(CLONE_NEWPID)       caller: own PID namespace unchanged
                            /proc/self/ns/pid              → old namespace
                            /proc/self/ns/pid_for_children → NEW namespace
fork()                      first child: PID 1 in the new namespace
fork() again                second child: PID 2 ... but only while PID 1 is alive
```

This is why the `unshare` command needs `--fork` with `--pid`:

```bash
sudo unshare --pid bash        # bash is NOT in the new namespace; its first child is PID 1,
                               # and when that child exits the namespace is dead:
                               # the next command fails with "fork: Cannot allocate memory"
sudo unshare --pid --fork bash # unshare forks; bash is PID 1 in the new namespace
```

### `setns(fd, CLONE_NEWPID)`

Same rule: it sets the PID namespace **for children**. The caller must then
`fork()` for anything to run inside. `nsenter --pid` does this automatically.
Joining is only allowed into the caller's own PID namespace or a descendant of
it: a process can never move "up" or "sideways" in the PID hierarchy.

## 5. Why `/proc` must be remounted

After `unshare --pid --fork bash`, try `ps`: it still shows **all host
processes**. Nothing is wrong with the PID namespace. `ps` reads `/proc`, and
the `/proc` currently mounted is a procfs instance that was mounted for the
**initial** PID namespace (Chapter 02 §5).

The fix is to mount a new procfs from inside the new PID namespace. To avoid
replacing `/proc` for the whole host, this must happen in a **new mount
namespace** as well:

```bash
sudo unshare --pid --fork --mount-proc bash
# --mount-proc = create a mount namespace, make mounts private,
#                and mount a new proc instance at /proc
```

This is the first example of namespaces depending on each other: PID isolation
is only *visible* when combined with a mount namespace and a fresh `/proc`.

## 6. Shell experiment

```bash
sudo unshare --pid --fork --mount-proc bash
echo $$            # 1
ps -ef             # only bash and ps
grep NSpid /proc/self/status
```

From the host, in another terminal:

```bash
ps -ef | grep 'unshare\|bash'
grep NSpid /proc/<host-pid-of-that-bash>/status
```

## 7. Minimal C example

`pid_clone.c` in the lab creates a child with `CLONE_NEWPID | CLONE_NEWNS`,
mounts a fresh `/proc` inside, and prints `getpid()`, `getppid()`, and the
`NSpid` line. The key lines:

```c
pid_t pid = clone(child_fn, stack_top, CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);

static int child_fn(void *arg) {
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);  // do not propagate to the host (§4 of Ch. 02)
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    printf("getpid()=%d getppid()=%d\n", getpid(), getppid());   // 1 and 0
    execlp("ps", "ps", "-ef", NULL);
}
```

## 8. How container runtimes use it

- Each container normally gets its own PID namespace. The application (or a tiny
  init such as `tini`) becomes PID 1 and inherits all PID 1 duties.
- The runtime mounts a new procfs at `/proc` in the container's mount namespace
  after the PID namespace exists.
- Kubernetes pods can share one PID namespace among their containers
  (`shareProcessNamespace: true`). Then only one process is PID 1 (the pod's
  "pause" container), which also reaps zombies for the pod.
- `docker run --pid=host` skips the PID namespace: the container can see host
  processes (commonly used by monitoring agents).
- The JVM in a container is often PID 1. `jps`, `jcmd`, and attach mechanisms
  use `/proc` and files named after PIDs under `/tmp/hsperfdata_*`; attaching
  from the host with a host PID versus a container PID is a direct consequence
  of the two numberings shown by `NSpid`.

## Evidence

Lab: [`lab-03-pid-namespace`](../../labs/03-namespaces/lab-03-pid-namespace/)

## Further Reading

- [`pid_namespaces(7)`](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html)
  — the authoritative rules: init process semantics, signals from ancestors,
  `unshare`/`setns` affecting only children, `/proc` and nesting limits (32
  levels). The most important reference for this section.
- [`unshare(1)`](https://man7.org/linux/man-pages/man1/unshare.1.html),
  options `--pid`, `--fork`, `--mount-proc`, and `--kill-child` — the tool
  documentation explains why `--fork` is needed.
- Michael Kerrisk, LWN, ["Namespaces in operation, part 3: PID namespaces"](https://lwn.net/Articles/531419/)
  and ["part 4: more on PID namespaces"](https://lwn.net/Articles/532748/) —
  experiments with nested namespaces, init death, and `getppid()` returning 0.
- Kernel source: [`kernel/pid_namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/pid_namespace.c),
  `zap_pid_ns_processes()` — the function that kills every process when a
  namespace's init exits. Short and directly tied to rule 3.
- [krallin/tini](https://github.com/krallin/tini) and the Kubernetes docs page
  [Share Process Namespace between Containers in a Pod](https://kubernetes.io/docs/tasks/configure-pod-container/share-process-namespace/)
  — practical consequences of PID 1 semantics.
