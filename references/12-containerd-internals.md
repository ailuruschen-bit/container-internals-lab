# References — Chapter 12: containerd Internals

Version note: written against **containerd 1.7 / 2.0**. Some package paths moved
under `core/` in 2.0 (noted inline). Links point to `main`; check
`containerd --version` and read the matching tag.

## containerd source and docs (primary)

| Path | What to read | Section |
|---|---|---|
| [README](https://github.com/containerd/containerd/blob/main/README.md), [docs/getting-started.md](https://github.com/containerd/containerd/blob/main/docs/getting-started.md) | orientation; the client walkthrough mirrors §5 | §1, §5 |
| [docs/](https://github.com/containerd/containerd/tree/main/docs) architecture, [content-flow.md](https://github.com/containerd/containerd/blob/main/docs/content-flow.md) | services and the content flow | §2, §3 |
| [docs/snapshotters/README.md](https://github.com/containerd/containerd/blob/main/docs/snapshotters/README.md) | the snapshotter interface and implementations | §3 |
| [api/services](https://github.com/containerd/containerd/tree/main/api/services) | the gRPC services (images, content, snapshots, containers, tasks) | §2 |
| [core/snapshots](https://github.com/containerd/containerd/tree/main/core/snapshots), [core/content](https://github.com/containerd/containerd/tree/main/core/content) | Snapshotter and content store (in 1.7: `snapshots/`, `content/`) | §3 |
| [core/runtime/v2/README.md](https://github.com/containerd/containerd/blob/main/core/runtime/v2/README.md) | the shim API and lifecycle (in 1.7: `runtime/v2/`) | §4 |
| [cmd/containerd-shim-runc-v2](https://github.com/containerd/containerd/tree/main/cmd/containerd-shim-runc-v2) | the runc shim binary | §4 |
| [client](https://github.com/containerd/containerd/tree/main/client) | `Pull`, `NewContainer`, `NewTask`, `task.Start` | §5 |
| [docs/cri/README.md](https://github.com/containerd/containerd/blob/main/docs/cri/README.md) | the CRI plugin | §6 |

## Kubernetes / CRI (primary for §6)

- Kubernetes: [Container Runtime Interface](https://kubernetes.io/docs/concepts/architecture/cri/),
  [Container Runtimes](https://kubernetes.io/docs/setup/production-environment/container-runtimes/).
- [cri-api](https://github.com/kubernetes/cri-api) — the CRI gRPC definitions.
- Kubernetes blog: [Don't Panic: Kubernetes and Docker](https://kubernetes.io/blog/2020/12/02/dont-panic-kubernetes-and-docker/)
  (dockershim removal).
- [pause container](https://github.com/kubernetes/kubernetes/tree/master/build/pause).

## Tools

- [`ctr`](https://github.com/containerd/containerd/blob/main/docs/getting-started.md)
  (ships with containerd) and [`nerdctl`](https://github.com/containerd/nerdctl)
  (a Docker-compatible CLI for containerd).

## Background / secondary

- Michael Crosby, ["What is containerd?"](https://www.docker.com/blog/what-is-containerd-runtime/)
  and the containerd shim design posts — the rationale for the shim (§4).
- CNCF: [containerd graduation](https://www.cncf.io/announcements/2019/02/28/cncf-announces-containerd-graduation/).
- Chapters 08 (OverlayFS), 10 (OCI), 11 (runc), and 01 §2 (subreapers) — the
  mechanisms containerd builds on.
