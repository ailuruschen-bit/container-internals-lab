# Lab 02 — Inspect an OCI Image: Index, Manifest, Config, Layers

## Goal

Produce evidence that:

1. an image is a content-addressed graph: index → manifest → config + layers;
2. the same layer digest is shared across images;
3. the image **config** carries `Entrypoint`, `Cmd`, `Env`, `User`, `WorkingDir`
   and `rootfs.diff_ids`;
4. these become the runtime `config.json` process fields.

## Prerequisites

- Linux VM (or any OS) with `skopeo` **or** `crane`, and `jq`. `docker` optional.
  - Debian/Ubuntu: `sudo apt-get install -y skopeo jq`
- Read: [4. The image spec](../../../docs/10-oci-runtime-spec/04-image-spec.md).

These commands talk to a registry read-only; no push, no auth for public images.

## Experiment

### Part A — The manifest (or index)

```bash
skopeo inspect --raw docker://docker.io/library/alpine:latest | jq .
```

If it is an **index** (multi-arch), it lists manifests per platform. Get the
amd64 manifest:

```bash
skopeo inspect --raw --override-os linux --override-arch amd64 \
  docker://docker.io/library/alpine:latest | jq '{config: .config, layers: [.layers[].digest]}'
```

**Predict first.** How many layers does Alpine have?

### Part B — The config (runtime metadata)

```bash
skopeo inspect --config docker://docker.io/library/alpine:latest | jq \
  '{Entrypoint: .config.Entrypoint, Cmd: .config.Cmd, Env: .config.Env,
    User: .config.User, WorkingDir: .config.WorkingDir, diff_ids: .rootfs.diff_ids}'
```

### Part C — Shared layers across images

```bash
BASE=$(skopeo inspect --override-os linux --override-arch amd64 docker://docker.io/library/alpine:latest | jq -r '.Layers[0]')
echo "alpine base layer: $BASE"
# An image built FROM alpine should share that first layer digest:
skopeo inspect --override-os linux --override-arch amd64 docker://docker.io/library/alpine:latest | jq '.Layers'
# Compare with another alpine-based image if you have one, e.g. a tagged build.
```

### Part D — Config becomes config.json

```bash
skopeo inspect --config docker://docker.io/library/nginx:latest | jq '.config | {Entrypoint, Cmd, ExposedPorts}'
```

Map these to the runtime spec fields from Chapter 10 §2:
`Entrypoint + Cmd → process.args`, `Env → process.env`, `User → process.user`,
`WorkingDir → process.cwd`.

### Part E (optional, crane) — the raw blobs

```bash
crane manifest alpine | jq .
crane config alpine | jq '.rootfs'
crane digest alpine
```

## Expected observations

**Part A.** For a multi-arch tag, `--raw` shows an index with `manifests[]`
carrying `platform` objects. The amd64 manifest lists one `config` descriptor and
a `layers` array (Alpine: **one** layer).

**Part B.** Alpine's config shows `Cmd=["/bin/sh"]`, an `Env` with `PATH`, empty
`Entrypoint`, `User` empty (root), and `rootfs.diff_ids` with one entry (matching
the single layer, but as the **uncompressed** digest).

**Part C.** The base layer digest is the same wherever Alpine is used; any image
`FROM alpine` lists that same digest as its bottom layer, so it is stored once.

**Part D.** nginx shows an `Entrypoint` (its launch script) and `Cmd`
(`["nginx","-g","daemon off;"]`) and `ExposedPorts` `{"80/tcp":{}}`.

**Part E.** `crane manifest`/`config` print the same objects `skopeo` shows.

## Why this happens

- Images are Merkle DAGs keyed by digest (Chapter 10 §4); tags resolve to a
  manifest/index digest.
- The config's `Env`/`Entrypoint`/`Cmd`/`User`/`WorkingDir` are the image's
  contribution to the runtime `config.json` process section (Chapter 10 §2, §4
  step 5).

## Connection to containers

- This is the input to the pull → unpack → overlay → bundle pipeline
  (Chapter 08 §3, Chapter 10 §4) that containerd runs (Chapter 12).
- `docker run nginx` uses exactly this config to decide what process to start;
  Chapter 13 traces it.

## Questions to think about

1. Why does pulling a second image that is `FROM alpine` download less than the
   first? Which digests are reused?
2. `docker run alpine echo hi` overrides `Cmd`. Which `config.json` field does the
   engine set, and does it keep the image's `Entrypoint`?
3. Why is a layer identified by two different digests (compressed vs `diff_id`)?
   Which one does a registry transfer, and which computes the overlay chain?
