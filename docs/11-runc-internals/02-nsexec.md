# 2. nsexec: the C Bootstrap

## Why C, and why before Go starts

Chapter 09 §1 listed the constraints that stop a Go program from setting up
namespaces cleanly: the Go runtime is multithreaded from startup, and

- `setns(CLONE_NEWNS)` and some operations require a **single-threaded** caller
  (Chapter 03 §4);
- `unshare(CLONE_NEWUSER)` requires single-threaded, and the uid/gid maps must be
  written by the **parent** while the child waits (Chapter 03 §7);
- `CLONE_NEWPID` puts the *child* at PID 1, so you must control which process
  becomes PID 1 (Chapter 03 §3).

`minic` sidestepped this by using Go's `SysProcAttr` (which does the clone and map
writing in the runtime's own C-level fork helper). runc needs more control, so it
ships a **C function, `nsexec()`, that runs before the Go runtime initializes**,
via a `__attribute__((constructor))`/cgo mechanism. At that moment the process is
single-threaded and can safely `clone`/`unshare`/`setns`.

File: [`libcontainer/nsenter/nsexec.c`](https://github.com/opencontainers/runc/blob/main/libcontainer/nsenter/nsexec.c),
compiled in via [`libcontainer/nsenter/nsenter.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/nsenter/nsenter.go)
(a cgo import). It only activates when the `_LIBCONTAINER_INITPIPE` environment
variable is present, i.e. in the re-exec'd `runc init`; otherwise it returns
immediately and normal Go `main` runs.

## Reading the bootstrap data

`nsexec` reads a **netlink-encoded** message from the init pipe (fd from
`_LIBCONTAINER_INITPIPE`). The message (built by the parent in
`process_linux.go`) contains: which namespaces to create or join, the uid/gid
map contents, the desired PID/`setgroups` policy, and related flags. This is the
"bootstrap data channel" from §1.

## The three-process dance

The single hardest thing to understand in runc is why `nsexec` uses **three**
processes (called stages), not one. It exists to satisfy the ordering rules of
user and PID namespaces simultaneously.

```text
stage-0  PARENT   (the original runc init, still in host namespaces)
   │ clone() → stage-1
   ▼
stage-1  CHILD    (unshares user ns; asks parent to write uid_map/gid_map)
   │ clone() → stage-2
   ▼
stage-2  INIT     (the container's PID 1, in the new PID ns; becomes runc init's Go code)
```

Why each hop:

- **stage-0 → stage-1**: stage-1 calls `unshare(CLONE_NEWUSER)` (and requests the
  other namespaces). It then signals stage-0, which, from **outside** the new
  user namespace, writes `/proc/<stage-1>/uid_map`, `gid_map`, and `setgroups`
  (only the parent can, Chapter 03 §7). stage-1 waits for the ack, then it has
  full capabilities inside the new user namespace and can create the remaining
  namespaces.
- **stage-1 → stage-2**: `CLONE_NEWPID` means the **new** process is PID 1 of the
  new PID namespace (Chapter 03 §3). So stage-1 must `clone` again to produce
  stage-2, which is the actual container init. stage-1 then reports stage-2's PID
  to the parent and exits.
- **stage-2** is the process that continues into the Go `init` code (§3, §4) and
  eventually `execve`s the container command.

The stages communicate over a small **sync socketpair**, sending typed messages
(`SYNC_USERMAP_PLS`/`SYNC_USERMAP_ACK`, `SYNC_RECVPID_PLS`/`_ACK`, etc.). Reading
`nsexec.c`'s big `switch (setjmp(...))` with these message names in hand makes it
tractable; do not try to read it linearly the first time.

## Mapping to what you did by hand

| nsexec does | You did it in |
|---|---|
| `unshare(CLONE_NEWUSER)` + parent writes maps | Chapter 03 Lab 07 (Part F) |
| create UTS/IPC/NET/mount namespaces | Chapter 03 Labs 02–06; `minic` `Cloneflags` |
| `CLONE_NEWPID` makes stage-2 PID 1 | Chapter 03 Lab 03; `minic` §2 |
| parent-writes-maps-while-child-waits ordering | `minic` omitted this (Chapter 09 §5/§7) |

`minic` collapsed all of this into `SysProcAttr` because the Go runtime's fork
helper does a simpler version. runc does it explicitly in C to control ordering,
support joining existing namespaces, and handle edge cases (rootless, nested user
namespaces, time namespaces) that `SysProcAttr` cannot express.

## After nsexec

When `nsexec` returns, the process (stage-2) is in all the requested namespaces,
single-threaded, and the Go runtime starts. Control passes to
`libcontainer.StartInitialization()` → the standard or setns initializer (§3).
From here it is Go again.

## Why this matters

- The `nsexec` three-process dance is the canonical answer to "how do you create
  a user namespace, write its maps from outside, and still end up as PID 1 of a
  new PID namespace, from a language with a multithreaded runtime?" Every
  Go-based runtime faces this.
- It explains observable facts: the transient `runc:[0:PARENT]`,
  `runc:[1:CHILD]`, `runc:[2:INIT]` process names you may catch in `ps` during
  container startup are these stages.

## Evidence

Lab: [`lab-01-trace-runc`](../../labs/11-runc-internals/lab-01-trace-runc/)

## Further Reading

- [`libcontainer/nsenter/nsexec.c`](https://github.com/opencontainers/runc/blob/main/libcontainer/nsenter/nsexec.c)
  — read the top comment and the `switch (setjmp(...))` stage machine, with the
  message names as your guide.
- [`libcontainer/nsenter/README.md`](https://github.com/opencontainers/runc/blob/main/libcontainer/nsenter/README.md)
  — runc's own explanation of why the C bootstrap exists.
- [`libcontainer/process_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/process_linux.go)
  — `newParentProcess` and where the bootstrap netlink data is built.
- Chapters 03 §3 (PID), §4 (mount/setns single-thread), §7 (user ns maps) — the
  rules nsexec is obeying.
