# 1. Layout and the Run Path

## Repository layout

runc has two conceptual halves:

```text
runc/
├── main.go, run.go, create.go, start.go, delete.go, ...   the CLI (OCI commands, Ch. 10 §3)
├── init.go                                                the hidden "runc init" subcommand
└── libcontainer/                                          the library that does the work
    ├── container_linux.go        the Container object and its state machine
    ├── process_linux.go          builds the child process (parent side)
    ├── init_linux.go             dispatch inside the re-exec'd child
    ├── standard_init_linux.go    the "created fresh" init path
    ├── setns_init_linux.go       the "join existing" init path (docker exec)
    ├── rootfs_linux.go           mounts, pivot_root, masked/readonly (Ch. 07)
    ├── nsenter/                   the C bootstrap: nsexec.c (Ch. 03, §2 here)
    ├── capabilities/             capability sets (Ch. 05)
    ├── seccomp/                  libseccomp profile compiler (Ch. 06)
    └── (cgroups via the opencontainers/cgroups module)   (Ch. 04)
```

The CLI half implements the OCI commands from Chapter 10 §3. The `libcontainer`
half is a reusable Go library (containerd's runc integration and others build on
it).

## The run path, at altitude

`runc run <id>` is `create` + `start` (Chapter 10 §3). Following `create`:

```text
run.go / create.go
   └─ libcontainer.Create(root, id, config)         build a Container from the OCI spec
   └─ container.Start(process) / container.Run(...)  start the init process
        └─ newParentProcess(container, process)      (process_linux.go)
             builds exec.Cmd for "/proc/self/exe init"  ← the re-exec (like minic, Ch. 09 §1)
             attaches pipes: the config/init pipe, the sync pipe, the FIFO for start
             sets cmd.SysProcAttr and the _LIBCONTAINER_* env vars (bootstrap data)
        └─ parent.start()
             cmd.Start()  → the kernel runs /proc/self/exe, but FIRST the C
                            constructor nsexec() runs (§2) and sets up namespaces
             then Go's init runs: libcontainer.StartInitialization() (§3, §4)
             parent and child exchange messages over the sync pipe to order setup
```

Two things to notice, both familiar:

1. **runc re-execs itself** as `runc init` (the `/proc/self/exe init` command),
   the same pattern `minic` used (Chapter 09 §1). The child is where in-container
   setup happens.
2. **A sync pipe orders the setup** between parent and child. This is the piece
   `minic` omitted (Chapter 09 §5): the parent does host-side work (write uid/gid
   maps, create the cgroup, run `createRuntime` hooks) at the exact points the
   child pauses for it, using request/ack messages (`procReady`, `procHooks`,
   etc. in `sync.go`).

## The two init paths

Inside the re-exec'd `runc init`, `init_linux.go` chooses an initializer:

| Init type | File | When |
|---|---|---|
| `standard` | `standard_init_linux.go` | a **new** container (`runc create/run`): create namespaces, rootfs, etc. |
| `setns` | `setns_init_linux.go` | **join** an existing container (`runc exec`, i.e. `docker exec`): `setns` into its namespaces, then exec |

The `setns` path is the production version of Chapter 03's `ns_join.c` and
Chapter 09 §7's "join" idea; the `standard` path is the production version of
`minic`'s `child()`.

## The bootstrap data channel

The parent cannot pass Go structs to code that runs before the Go runtime starts
(nsexec, §2). Instead it encodes the namespaces to create, the uid/gid maps, and
related data as **netlink messages** and writes them to the init pipe; nsexec (in
C) reads them. The higher-level config (the full OCI spec) is passed separately
to the Go init over the config pipe as JSON. Keeping these two channels straight
(C bootstrap data vs Go config) is key to not getting lost.

## Why this matters

- The overall shape is `minic` with the rough edges made correct: re-exec, a sync
  pipe for ordering, two init paths, and a C bootstrap for the parts Go cannot do
  safely. Recognizing this makes the code approachable.
- The next sections zoom into the three hard parts: the C namespace bootstrap
  (§2), the rootfs (§3), and the privilege reduction + exec (§4).

## Further Reading

- runc: [`run.go`](https://github.com/opencontainers/runc/blob/main/run.go),
  [`create.go`](https://github.com/opencontainers/runc/blob/main/create.go),
  [`init.go`](https://github.com/opencontainers/runc/blob/main/init.go).
- [`libcontainer/process_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/process_linux.go)
  — `newParentProcess`, `initProcess.start()`, the sync loop.
- [`libcontainer/init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/init_linux.go)
  — `StartInitialization()` and the init-type dispatch.
- [`libcontainer/sync.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/sync.go)
  — the parent/child sync protocol that `minic` lacked.
- runc [README](https://github.com/opencontainers/runc/blob/main/README.md) and
  [docs/](https://github.com/opencontainers/runc/tree/main/docs) for orientation.
