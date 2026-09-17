# Lab 01 — ctr and the containerd Services

## Goal

Produce evidence that:

1. containerd is a daemon with a gRPC socket and pluggable services;
2. `ctr` exercises the images, content, snapshots, containers, and tasks
   services separately;
3. containerd namespaces (`default`, `k8s.io`, `moby`) scope objects and are
   **not** Linux namespaces.

## Prerequisites

- Linux VM with `containerd` running, `ctr` (ships with containerd), `sudo`,
  `jq`. A Docker host works (`ctr` is present; use `-n moby` to see Docker's
  objects).
- Read: [2. Architecture and services](../../../docs/12-containerd-internals/02-architecture.md).

## Experiment

### Part A — The daemon and its plugins

```bash
sudo ctr version
ls -l /run/containerd/containerd.sock
sudo ctr plugins ls | grep -E 'snapshotter|runtime|content|cri' | head
```

### Part B — containerd namespaces (not Linux namespaces)

```bash
sudo ctr namespaces ls
sudo ctr -n default images ls 2>/dev/null | head
sudo ctr -n moby   containers ls 2>/dev/null | head      # Docker's, if this is a Docker host
sudo ctr -n k8s.io containers ls 2>/dev/null | head      # Kubernetes', if a node
```

**Predict first.** Are these the same kind of namespace as Chapter 03's?

### Part C — Pull touches content, snapshots, images

```bash
sudo ctr image pull docker.io/library/alpine:latest
sudo ctr images ls | grep alpine
sudo ctr content ls | head
sudo ctr snapshots ls | head
```

### Part D — A container and a task

```bash
sudo ctr run -d docker.io/library/alpine:latest demo sleep 300
sudo ctr containers ls
sudo ctr tasks ls
sudo ctr task exec --exec-id e1 demo /bin/sh -c 'hostname; echo $$; ps'
sudo ctr task kill demo; sleep 1; sudo ctr tasks ls
sudo ctr container rm demo
```

## Expected observations

**Part A.** `ctr version` prints client and server versions. The socket exists.
`plugins ls` lists `io.containerd.snapshotter.v1.overlayfs`,
`io.containerd.runtime.v2.task`, content, and (on a k8s/Docker host) the CRI
plugin, all in state `ok`.

**Part B.** `namespaces ls` shows `default` and, depending on the host, `moby`
and/or `k8s.io`. Objects differ per namespace. These are containerd's
multi-tenancy labels, unrelated to Linux namespaces.

**Part C.** The image appears in `images ls`; `content ls` shows blobs (manifest,
config, layers) by digest; `snapshots ls` shows the unpacked layer snapshots.

**Part D.** `containers ls` shows `demo` (the definition); `tasks ls` shows it
running with a PID. `task exec` runs a second process **inside** the container
(its own hostname, small PID, few processes). `task kill` stops it.

## Why this happens

- containerd is one daemon exposing services over gRPC (§2). `ctr` is a thin
  client that calls each service.
- Pull populates content + snapshots + images (§3); run creates a Container then a
  Task via the shim (§4).

## Connection to containers

- `docker` and `kubelet` call these same services; `-n moby` and `-n k8s.io` let
  you see their objects directly.
- `ctr task exec` is `docker exec` / `kubectl exec` at the containerd level (the
  runc setns path, Chapter 11 §1).

## Questions to think about

1. Why does containerd need its own "namespaces" concept separate from Linux
   namespaces? What problem do they solve?
2. Which service would you inspect to debug "image pulled but container won't
   start", vs "container starts but immediately exits"?
3. On a Docker host, run `sudo ctr -n moby containers ls`. Why do Docker's
   containers appear here?
