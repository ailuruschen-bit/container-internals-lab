# Lab 02 — The Content Store and Snapshots

## Goal

Produce evidence that:

1. an image's manifest, config, and layers are blobs in the content store, keyed
   by digest;
2. layers are unpacked into committed snapshots, shared across images;
3. a running container gets its own active snapshot (writable upper layer);
4. this is the OverlayFS of Chapter 08, managed by containerd.

## Prerequisites

- Linux VM with `containerd`, `ctr`, `sudo`, `jq`.
- Read: [3. Content, images, and snapshotters](../../../docs/12-containerd-internals/03-content-and-snapshots.md)
  and Chapter 08.

## Experiment

### Part A — Content blobs by digest

```bash
sudo ctr image pull docker.io/library/alpine:latest
sudo ctr content ls | head
DIGEST=$(sudo ctr images ls | awk '/alpine/{print $3; exit}')
echo "manifest digest: $DIGEST"
sudo ctr content get "$DIGEST" | jq '.config.digest, [.layers[].digest]'
```

### Part B — On-disk layout

```bash
sudo ls /var/lib/containerd/io.containerd.content.v1.content/blobs/sha256/ | head
sudo du -sh /var/lib/containerd/io.containerd.content.v1.content
```

### Part C — Snapshots, committed and active

```bash
sudo ctr snapshots ls | head
sudo ctr run -d docker.io/library/alpine:latest demo sleep 300
sudo ctr snapshots ls | grep -E 'demo|Active|Committed' | head
sudo ctr snapshots tree 2>/dev/null | head -n 20 || sudo ctr snapshots ls
```

### Part D — The overlay mount of the running container

```bash
PID=$(sudo ctr task ls | awk '/demo/{print $2}')
sudo grep ' / ' /proc/$PID/mountinfo | grep overlay
```

**Predict first.** Will the rootfs mount be type `overlay`, with `lowerdir`
(committed snapshots) and `upperdir` (the container's active snapshot)?

### Part E — Shared layers

```bash
sudo ctr image pull docker.io/library/alpine:3.19 2>/dev/null || true
sudo ctr snapshots ls | wc -l
# Two alpine images sharing a base add fewer snapshots than 2x a full image.
```

Cleanup:

```bash
sudo ctr task kill demo 2>/dev/null; sleep 1
sudo ctr container rm demo 2>/dev/null
```

## Expected observations

**Part A.** `content get` on the manifest shows a `config.digest` and a `layers`
array of digests (Alpine: one layer). These are the Chapter 10 §4 objects.

**Part B.** The blobs directory contains files named by digest; total size is a
few MB for Alpine.

**Part C.** `snapshots ls` shows committed snapshots (the unpacked layers) and,
after `ctr run`, an **active** snapshot for `demo` (its writable layer).

**Part D.** The rootfs mount is `overlay`, with `lowerdir` pointing at committed
snapshot directories and `upperdir`/`workdir` at the active snapshot — exactly the
Chapter 08 Lab 01 structure, produced by containerd.

**Part E.** Pulling a second Alpine that shares the base layer adds far fewer new
snapshots/blobs than a wholly different image would: dedup by digest (Chapter 10
§4).

## Why this happens

- The content store keys blobs by digest (§3); the snapshotter unpacks layers to
  committed snapshots and prepares an active snapshot per container, returning
  overlay mounts (§3, Chapter 08 §2–3).

## Connection to containers

- This is the concrete link between "an image" and "the rootfs runc pivots into":
  containerd's content + snapshots services do Chapter 08 §3's unpack + overlay,
  and Chapter 10 §4's image → bundle.
- `docker` and Kubernetes use these same snapshots.

## Questions to think about

1. Map the `overlay` mount in Part D to Chapter 08 Lab 01's `lowerdir`/`upperdir`/
   `workdir`. Which directory is shared with other Alpine containers?
2. If you write a 100 MB file inside `demo`, which snapshot grows, and what
   happens to it when you `container rm demo`?
3. What would a lazy-pulling snapshotter (stargz/nydus) change about Part A/B?
