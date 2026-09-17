# 4. Containers, Tasks, and the Shim

This is the most important section of the chapter: the **Container/Task split**
and the **shim**, the parts that most distinguish a high-level runtime from runc.

## Container vs Task

containerd separates the **static definition** from the **running process**:

| Concept | Is | Analogy |
|---|---|---|
| **Container** | metadata: the OCI spec, the chosen snapshot, the runtime to use, labels | a saved configuration |
| **Task** | the actual running process (with a PID), created from a container | an execution of it |

```text
ctr container create ...   → a Container object (no process yet)   (like writing config.json)
ctr task start <id>        → a Task: the process runs               (like runc create + start)
```

You can create a container, then start/stop/delete tasks from it. A task has a
PID, exit status, and can host `exec`'d extra processes. This mirrors the OCI
created/running split (Chapter 10 §3) one level up.

## Why a shim must exist

When the Tasks service creates a task, it does **not** run runc as a direct child
of the containerd daemon. It starts a small, long-lived **shim** process
(`containerd-shim-runc-v2`), one per container, which invokes runc. Why go to
this trouble?

Recall Chapter 01 §2: a process's parent must reap it and receives its exit
status; if the parent dies, children are reparented. If containerd ran the
container process as its own child:

1. **Restarting containerd would kill or orphan every container.** With a shim,
   the shim is the container's parent; containerd can restart, and the containers
   keep running, re-attaching to the shims afterward.
2. **The exit code would be lost on a daemon crash.** The shim, as the
   container's parent (a subreaper, Chapter 01 §2), holds the exit status until
   containerd collects it.
3. **stdio/TTY must be owned by something persistent.** The shim owns the
   container's stdin/stdout/stderr (or its pty), buffering and forwarding logs
   even while no client is attached.
4. **runc must not linger.** With Runtime v2, runc does its setup and **exits**;
   the shim remains as the supervisor. This keeps per-container memory low (no
   idle runc processes).

The shim is therefore the answer to "who is PID-1-adjacent for the container on
the host" — it is the reaping, I/O-owning, restart-surviving parent. It uses
`PR_SET_CHILD_SUBREAPER` (Chapter 01 §2, Lab 02) so that even if the container
double-forks, the shim still reaps it.

## The Runtime v2 shim API

The shim exposes a small API (a ttRPC service over a socket) that the Tasks
service drives:

```text
containerd daemon ──ttRPC──► shim ──► runc
   Create   → shim: prepare, runc create (bundle from Ch. 10)
   Start    → shim: runc start (the process execs; created→running)
   Wait     → shim: report the exit code when the process exits
   Kill     → shim: runc kill (signal, Ch. 01 §6)
   Exec     → shim: runc exec (a new process in the container; the setns path, Ch. 11 §1)
   Pause/Resume → cgroup freezer (Ch. 04 Lab 01)
   Delete   → shim: runc delete; then the shim exits
```

Notice these map onto runc's OCI operations (Chapter 10 §3, Chapter 11): the shim
is a thin, persistent wrapper that turns containerd's gRPC task calls into runc
CLI invocations plus supervision.

## The process tree on the host

```text
systemd
├── containerd                 (the daemon; can restart independently)
└── containerd-shim-runc-v2    (one per container; the container's real parent)
    └── <container PID 1>       (your application; runc exited after setup)
        └── ...children...
```

Run `ps -ef --forest` on a host with containers and you will see the shims as the
parents of the container processes, **not** containerd. That is the visible proof
of this design and of Chapter 01 §2's reaping rules.

## Runtime plugins: not only runc

The v2 shim interface lets containerd use different low-level runtimes by shipping
different shims: `containerd-shim-runc-v2` (runc/crun), a Kata shim (VM-isolated),
a gVisor `runsc` shim (user-space kernel). The container's chosen runtime (a field
on the Container object) selects the shim. This is the same OCI-driven
pluggability as Chapter 12 §1, realized through shims.

## Why this matters

- The shim is the single most under-appreciated component in the stack, and it is
  a direct application of Chapter 01 §2 (subreapers, reaping, exit status). This
  section is where "process fundamentals" pay off at the top of the stack.
- The Container/Task split and the shim explain real behavior: containers
  surviving a Docker/containerd restart, where logs come from, why you see
  `containerd-shim` processes, and how `exec` works.

## Evidence

Lab: [`lab-03-tasks-and-shim`](../../labs/12-containerd-internals/lab-03-tasks-and-shim/)

## Further Reading

- containerd [runtime v2 docs](https://github.com/containerd/containerd/blob/main/core/runtime/v2/README.md)
  (in 1.7: `runtime/v2/README.md`) — the shim API and lifecycle.
- containerd [PLUGINS.md](https://github.com/containerd/containerd/blob/main/docs/PLUGINS.md)
  and the runc shim under [`cmd/containerd-shim-runc-v2`](https://github.com/containerd/containerd/tree/main/cmd/containerd-shim-runc-v2).
- Michael Crosby, ["What is containerd?"](https://www.docker.com/blog/what-is-containerd-runtime/)
  and the containerd shim design blog posts — the rationale for the shim.
- Chapter 01 §2 (subreapers, reaping) and Lab 02 — the mechanism the shim relies
  on.
