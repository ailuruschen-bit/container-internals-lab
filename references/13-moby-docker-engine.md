# References — Chapter 13: Moby / Docker Engine

Version note: written against **Docker Engine / Moby 27.x** (uses containerd and
runc underneath). Links point to `moby/moby` `master` and current Docker docs;
check `docker version`.

## Docker documentation (primary)

| Document | Why read it | Section |
|---|---|---|
| [Docker overview](https://docs.docker.com/get-started/docker-overview/) | the CLI/daemon/containerd/runc architecture | §1 |
| [Engine API reference](https://docs.docker.com/reference/api/engine/) | the REST API the CLI uses | §1, §4 |
| [Networking overview](https://docs.docker.com/engine/network/), [Bridge driver](https://docs.docker.com/engine/network/drivers/bridge/), [Packet filtering and firewalls](https://docs.docker.com/engine/network/packet-filtering-firewalls/) | docker0, veth, masquerade, DNAT (Ch. 03 §6) | §2 |
| [Dockerfile reference](https://docs.docker.com/reference/dockerfile/), [Build with BuildKit](https://docs.docker.com/build/buildkit/) | how a build produces OCI layers | §3 |
| [docker run reference](https://docs.docker.com/reference/cli/docker/container/run/) | the command traced in §4 | §4 |

## Moby / BuildKit / libnetwork source (primary)

- [moby/moby](https://github.com/moby/moby) — the Docker Engine (`dockerd`); the
  [`daemon/`](https://github.com/moby/moby/tree/master/daemon) package handles
  container create/start and the containerd integration.
- [moby/moby libnetwork](https://github.com/moby/moby/tree/master/libnetwork)
  (formerly [moby/libnetwork](https://github.com/moby/libnetwork)) — the bridge
  driver and CNM (§2).
- [moby/buildkit](https://github.com/moby/buildkit) — the builder and LLB (§3).
- [moby/moby oci/](https://github.com/moby/moby/tree/master/oci) — where Docker
  builds the OCI runtime spec (e.g. default capabilities, Ch. 05 §5).

## Lower layers (the rest of the stack)

- Chapters 11 (runc) and 12 (containerd) references — `dockerd` sits directly on
  containerd, which sits on runc.
- Chapter 10 (OCI) — the image and runtime specs Docker produces and consumes.
- Chapters 01–08 — the kernel mechanisms the traced process ends up using.

## Background / history

- Docker blog: [What is containerd?](https://www.docker.com/blog/what-is-containerd-runtime/)
  and the runc/containerd donation history (Ch. 10 §1, 12 §1).
- Kubernetes blog: [Don't Panic: Kubernetes and Docker](https://kubernetes.io/blog/2020/12/02/dont-panic-kubernetes-and-docker/)
  — why Kubernetes uses containerd directly (Ch. 12 §6).
