# 2. Architecture and Services

## A daemon of services

containerd is a single daemon exposing a **gRPC API** over a Unix socket
(`/run/containerd/containerd.sock`). Internally it is organized as a set of
**services** (implemented as plugins), each owning one concern. Clients call the
services; the services call each other and, ultimately, a shim that runs runc.

```text
client (ctr / dockerd / kubelet)
        │ gRPC over /run/containerd/containerd.sock
        ▼
┌───────────────────────── containerd daemon ─────────────────────────┐
│  Images service     ── metadata: image name → manifest digest        │
│  Content service    ── the content store: blobs by digest (Ch. 10 §4)│
│  Snapshots service  ── snapshotters: unpack layers, mounts (Ch. 08)  │
│  Diff service       ── apply/compare layer diffs                      │
│  Containers service ── container metadata (spec, snapshot, runtime)   │
│  Tasks service      ── the RUNNING process: create/start/kill/exec    │
│  Namespaces (containerd's own, NOT Linux ns) ── multi-tenant scoping  │
└──────────────────────────────────────────────────────────────────────┘
        │ Tasks service starts a shim per container
        ▼
   containerd-shim-runc-v2  ──►  runc  ──►  the container process
```

Two naming cautions:

- containerd **namespaces** (e.g. `default`, `moby`, `k8s.io`) are a
  multi-tenancy label for containerd's own objects (images, containers). They are
  **not** Linux namespaces (Chapter 03). `ctr -n k8s.io ...` lists Kubernetes'
  objects.
- A containerd **image** object is just metadata (a name pointing at a manifest
  digest in the content store); the bytes live in the **content** service, and the
  usable filesystem is produced by the **snapshots** service.

## The key services for one container

Following a container from image to running process touches these in order:

| Service | Role | Chapter |
|---|---|---|
| Content | store the pulled manifest, config, and layer blobs by digest | 10 §4 |
| Snapshots | unpack layers into snapshots and produce mounts for the rootfs | 08 §2–3 |
| Images | record the image name → manifest mapping | 10 §4 |
| Containers | store the container's OCI spec, chosen snapshot, and runtime | 10 §2 |
| Tasks | create the actual process via a shim + runc; supervise it | 11 |

The **Containers** vs **Tasks** split is fundamental and is covered in §4: a
*container* is configuration and storage; a *task* is the running process.

## The runtime is a shim, invoked as a plugin

The Tasks service does not call runc directly. It starts a **shim** process
(`containerd-shim-runc-v2`) per container, and talks to it over a small API. The
shim invokes runc and stays alive to supervise the container. This is the
**Runtime v2** design, and §4 explains why the shim must exist.

## State and plugins

- containerd keeps metadata in a local database (bolt) under
  `/var/lib/containerd`, and content/snapshots on disk there too. This is the
  "survives daemon restart" state.
- Almost everything is a **plugin**: snapshotters (overlayfs, native, ...),
  runtimes (the runc shim, others), the CRI service (§6), content and metadata.
  `ctr plugins ls` shows them. This plugin model is how containerd stays a stable
  core with swappable parts.

## Why this matters

- Knowing the services tells you *where* each responsibility lives, so when you
  read the code or debug, you know whether a problem is "content/snapshot"
  (image/storage), "tasks/shim" (running), or "CRI" (Kubernetes).
- The Containers/Tasks split and the shim are the two ideas that most distinguish
  a high-level runtime from runc; §4 develops them.

## Evidence

Lab: [`lab-01-ctr-and-services`](../../labs/12-containerd-internals/lab-01-ctr-and-services/)

## Further Reading

- containerd [architecture overview](https://github.com/containerd/containerd/blob/main/docs/README.md)
  and the [content](https://github.com/containerd/containerd/blob/main/docs/content-flow.md)
  and [snapshotters](https://github.com/containerd/containerd/blob/main/docs/snapshotters/README.md)
  docs.
- containerd API protos: [`api/services`](https://github.com/containerd/containerd/tree/main/api/services)
  — the gRPC services listed above.
- [`ctr`](https://github.com/containerd/containerd/blob/main/docs/getting-started.md)
  — the low-level client used in the lab to poke each service.
