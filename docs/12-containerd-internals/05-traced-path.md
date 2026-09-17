# 5. A Traced Creation Path

This section follows one command, `ctr run docker.io/library/alpine:latest demo
/bin/sh`, from the client to a running task, naming the service and the chapter at
each step. It is the containerd equivalent of Chapter 11's runc trace.

## The path

```text
ctr run alpine:latest demo /bin/sh
│
├─ (1) resolve + pull                                        Ch. 10 §4, Ch. 12 §3
│      Images/Content: fetch manifest, config, layer blobs into the content store
│      Diff/Snapshots: unpack layers → committed snapshots
│
├─ (2) create the Container object                           Ch. 12 §4, Ch. 10 §2
│      build the OCI runtime spec (config.json) from:
│        - the image config (Entrypoint/Cmd/Env/User/WorkingDir)   Ch. 10 §4
│        - ctr's options + containerd defaults (namespaces, mounts, caps, seccomp)
│      choose the snapshotter (overlayfs) and runtime (io.containerd.runc.v2)
│      record it in the metadata DB
│
├─ (3) create the Task                                       Ch. 12 §4, Ch. 11
│      Snapshots.Prepare("demo"): active snapshot + overlay mounts   Ch. 08 §2
│      Tasks service starts the shim: containerd-shim-runc-v2
│      shim assembles the bundle (config.json + rootfs from the mounts)  Ch. 10 §2
│      shim → runc create demo:
│         nsexec (namespaces + maps) → rootfs/pivot_root → caps → no_new_privs   Ch. 11 §2-4
│         the init blocks on the exec FIFO  (state: created)         Ch. 10 §3
│      shim starts owning the task's stdio/pty and becomes its subreaper  Ch. 01 §2
│
├─ (4) start the Task                                        Ch. 11 §4, Ch. 10 §3
│      Tasks.Start → shim → runc start demo:
│         the FIFO is written; the init execve()s /bin/sh (state: running)  Ch. 01 §3
│
└─ (5) supervise                                             Ch. 12 §4, Ch. 01 §2
       shim waits on the process; on exit it holds the exit code for containerd
       logs flow through the shim; kill/exec/pause go shim → runc
```

## What each layer contributed

| Layer | Contribution | Chapter |
|---|---|---|
| Registry / Content / Snapshots | image bytes → committed snapshots | 10 §4, 08 §3 |
| Containers service | image config + options → `config.json` | 10 §2, §4 |
| Snapshots.Prepare | writable upper + overlay mounts → rootfs | 08 §2 |
| Tasks + shim | invoke runc, own stdio, supervise, reap | 12 §4, 01 §2 |
| runc | apply the bundle once: namespaces, cgroups, rootfs, caps, seccomp, execve | 11 |
| kernel | the actual isolation and resource control | 01–08 |

Read bottom-up, this is the whole repository: the kernel mechanisms (01–08),
applied by runc (11) from a bundle (10), on a rootfs from overlay snapshots (08),
all orchestrated and supervised by containerd (12).

## The same path under Docker and Kubernetes

- **Docker** (Chapter 13): `docker run` → `dockerd` builds the container config
  and calls this exact containerd path over gRPC; `dockerd` adds image build,
  networking (libnetwork), and volumes on top.
- **Kubernetes** (§6): the kubelet calls the **CRI** service in containerd, which
  drives the same content/snapshots/containers/tasks path, once per pod sandbox
  and once per container.

So `docker run nginx` and a Kubernetes pod both reduce to steps (1)–(5) above.
Chapter 13 makes the Docker case explicit.

## Why this matters

- This is the connective tissue: it shows precisely where the image spec, the OCI
  runtime spec, OverlayFS, runc, and the kernel mechanisms each act in one real
  command.
- It sets up Chapter 13, which only needs to add the Docker daemon's own
  responsibilities (build, network, volumes, API) on top of this path.

## Evidence

Lab: [`lab-03-tasks-and-shim`](../../labs/12-containerd-internals/lab-03-tasks-and-shim/)
(the process tree) and [`lab-01-ctr-and-services`](../../labs/12-containerd-internals/lab-01-ctr-and-services/)
(the services).

## Further Reading

- containerd [getting started](https://github.com/containerd/containerd/blob/main/docs/getting-started.md)
  — the client walkthrough that mirrors steps (1)–(4) in code.
- containerd client package [`client`](https://github.com/containerd/containerd/tree/main/client)
  — `Pull`, `NewContainer`, `NewTask`, `task.Start` map to steps (1)–(4).
