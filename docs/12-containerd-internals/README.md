# Chapter 12 — containerd Internals

## Why this chapter exists

runc (Chapter 11) runs **one** container from a bundle and exits. That leaves a
lot undone: something must pull and store images, unpack them into snapshots
(Chapter 08), build the OCI bundle (Chapter 10), invoke runc, **keep the
container supervised after runc exits**, collect its output and exit code, and
expose all of this through a stable API to many clients. That something is a
**container runtime daemon**. **containerd** is the dominant one: it runs under
Docker, and it is the default container runtime for Kubernetes (via CRI).

This chapter explains containerd's architecture and responsibilities, and traces
one container-creation path, so you can see the division of labor between the
**high-level runtime** (containerd: images, storage, supervision, API) and the
**low-level runtime** (runc: apply the OCI bundle once). We do not read all of
containerd; we map it and follow one path, as with runc.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [Why a layer above runc](01-why-containerd.md) | The jobs runc does not do; high- vs low-level runtimes. |
| 2 | [Architecture and services](02-architecture.md) | Client → gRPC daemon → services → shim → runc. |
| 3 | [Content, images, and snapshotters](03-content-and-snapshots.md) | Pull, the content store, unpack to snapshots (Chapter 08/10). |
| 4 | [Containers, tasks, and the shim](04-tasks-and-shim.md) | Why a shim exists; how runc is invoked and the container supervised. |
| 5 | [A traced creation path](05-traced-path.md) | From `ctr run` to a running task, step by step. |
| 6 | [CRI: containerd under Kubernetes](06-cri.md) | Pods, the CRI plugin, sandbox and containers. |
| 7 | [Chapter summary](07-summary.md) | What changed, what did not. |

Labs: [`labs/12-containerd-internals/`](../../labs/12-containerd-internals/).
References: [`references/12-containerd-internals.md`](../../references/12-containerd-internals.md).

## Prerequisites

- **Chapters 08 (OverlayFS), 10 (OCI), 11 (runc).** This chapter connects them.
- Chapter 01 §2 (subreapers, reaping) is important for the shim (§4).

## Versions

Written against **containerd 1.7 / 2.0** (the series current as of writing).
Package paths and the shim binary name are stable across these, but internals
change; the prompt's rule applies. Links point to `main`; check your version
(`containerd --version`).

## Environment

A Linux VM with `containerd` and its CLI `ctr` (and optionally `nerdctl`),
`sudo`, and `jq`. Docker also bundles containerd, so a Docker host works too.
