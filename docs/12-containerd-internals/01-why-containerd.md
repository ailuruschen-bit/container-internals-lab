# 1. Why a Layer Above runc

## What runc does not do

runc is deliberately minimal: given a bundle (`config.json` + rootfs), it applies
the OCI configuration once and either runs the process (`runc run`) or sets it up
and exits (`runc create`/`start`, Chapter 10 §3). It knows nothing about:

| Missing job | Why it is needed |
|---|---|
| **Images** | runc needs a rootfs directory; someone must pull image layers from a registry and store them (Chapter 10 §4). |
| **Storage / snapshots** | someone must unpack layers and overlay-mount them into the rootfs (Chapter 08 §3). |
| **Bundle construction** | someone must translate the image config + user options into `config.json` (Chapter 10 §2, §4). |
| **Supervision** | runc `run` blocks; runc `create/start` exits. Something must keep watching the container process, collect its exit code, and manage its stdio **even across daemon restarts**. |
| **Lifecycle at scale** | start/stop/pause/exec/delete for **many** containers, with state that survives reboots. |
| **A stable API** | many clients (the Docker daemon, Kubernetes, `ctr`, `nerdctl`) need one programmatic interface. |
| **Distribution** | push/pull over the registry API (Chapter 10 §4). |

## High-level vs low-level runtime

The ecosystem splits these responsibilities into two layers, decoupled by the OCI
bundle + runtime CLI contract (Chapter 10):

```text
             high-level runtime (containerd)
   images · content store · snapshots · bundles · API · supervision
                              │  invokes, per container, via a shim
                              ▼
             low-level runtime (runc, crun, youki, Kata, runsc)
              apply one OCI bundle: namespaces, cgroups, rootfs, ...
                              │
                              ▼
                        Linux kernel (Chapters 01-08)
```

This separation is why you can swap runc for crun (a C runtime) or gVisor's runsc
(a user-space kernel) or Kata (a lightweight VM) **without changing containerd**:
they all honor the OCI runtime spec. And you can swap containerd for CRI-O under
Kubernetes without changing runc.

## Where containerd sits

```text
docker CLI ─┐
Kubernetes ─┼─► containerd (daemon, gRPC API) ─► shim ─► runc ─► kernel
nerdctl / ctr ─┘
```

containerd is a **daemon** (`containerd`) that clients talk to over a gRPC API on
a Unix socket. It is the high-level runtime for both Docker (Chapter 13) and
Kubernetes (§6). Docker's own daemon `dockerd` uses containerd underneath.

## What this chapter will and will not do

- **Will:** map containerd's services (§2), explain the content store and
  snapshotters (§3), explain containers vs tasks and the crucial **shim** (§4),
  trace one creation (§5), and connect to Kubernetes via CRI (§6).
- **Will not:** document every service or plugin. containerd is large; as with
  runc, we follow one path and build a mental map.

## Why this matters

- It clarifies a common confusion: "Docker vs containerd vs runc" is not
  competition; they are **layers**. runc does the kernel work once; containerd
  manages images, storage, supervision, and the API; Docker/Kubernetes sit on
  top.
- The shim (§4) is the piece most people have never heard of but is essential:
  it is why a container keeps running when you restart containerd or Docker.

## Further Reading

- containerd [README](https://github.com/containerd/containerd/blob/main/README.md)
  and [Getting Started](https://github.com/containerd/containerd/blob/main/docs/getting-started.md).
- containerd [architecture docs](https://github.com/containerd/containerd/blob/main/docs/) —
  orientation for §2.
- CNCF, ["containerd graduation"](https://www.cncf.io/announcements/2019/02/28/cncf-announces-containerd-graduation/)
  — context on its role in the ecosystem.
