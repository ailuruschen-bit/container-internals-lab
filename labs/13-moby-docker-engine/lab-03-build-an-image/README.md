# Lab 03 — Build an Image and See the Layers

## Goal

Confirm that `docker build` produces the OCI layers of Chapters 08 and 10 §4, that
instructions map to layers, and that the build cache is content-addressed.

## Prerequisites

- Linux VM with Docker (BuildKit is the default builder), `jq`.
- Read: Chapter 13 [§3](../../../docs/13-moby-docker-engine/03-build.md),
  Chapters 08 and 10 Lab 02.

## Experiment

### Part A — A small Dockerfile

```bash
mkdir -p /tmp/buildlab && cd /tmp/buildlab
cat > Dockerfile <<'EOF'
FROM alpine:latest
RUN echo "layer from RUN" > /note.txt
COPY hello.txt /hello.txt
ENV GREETING=hi
CMD ["cat", "/note.txt", "/hello.txt"]
EOF
echo "layer from COPY" > hello.txt
docker build -t buildlab:v1 .
```

### Part B — Instructions became layers

```bash
docker history buildlab:v1
docker image inspect buildlab:v1 | jq '.[0].RootFS.Layers, .[0].Config.Cmd, .[0].Config.Env'
```

**Predict first.** How many filesystem layers did `RUN` and `COPY` add on top of
alpine? Did `ENV`/`CMD` add layers?

### Part C — The cache is content-addressed

```bash
docker build -t buildlab:v2 .            # no changes → all cached
echo "changed" > hello.txt
docker build -t buildlab:v3 .            # only the COPY layer and after rebuild
```

**Predict first.** After changing `hello.txt`, which instructions rebuild and
which stay cached?

### Part D — It is an OCI image

```bash
docker save buildlab:v1 -o /tmp/buildlab.tar
mkdir -p /tmp/buildlab-img && tar -xf /tmp/buildlab.tar -C /tmp/buildlab-img
ls /tmp/buildlab-img
cat /tmp/buildlab-img/manifest.json | jq '.[0].Layers'
```

### Part E — Run it (closes the loop to Lab 01)

```bash
docker run --rm buildlab:v1
```

Cleanup: `docker rmi buildlab:v1 buildlab:v2 buildlab:v3 2>/dev/null; rm -rf /tmp/buildlab /tmp/buildlab.tar /tmp/buildlab-img`.

## Expected observations

**Part A.** The build runs each instruction; BuildKit prints steps and caching.

**Part B.** `docker history` shows the alpine base layers plus one layer for
`RUN` and one for `COPY`; `ENV` and `CMD` appear as zero-byte metadata steps (no
filesystem layer). `RootFS.Layers` lists the `diff_id`s (Chapter 10 §4); `Cmd` and
`Env` are in the config (Chapter 10 §4).

**Part C.** `v2` is fully cached (identical inputs → identical layer digests,
Chapter 10 §4). After editing `hello.txt`, the `COPY` step and everything after it
rebuild; the `FROM` and `RUN` layers stay cached (order matters for caching).

**Part D.** The saved image expands to a `manifest.json` listing the layers and a
config — the same OCI structure as Chapter 10 Lab 02.

**Part E.** The container prints the contents of `/note.txt` and `/hello.txt`
(from the two layers), using the `CMD` from the config.

## Why this happens

- Each filesystem-changing instruction produces a layer (a diff, Chapter 08 §3);
  metadata instructions set the image config (Chapter 10 §4). BuildKit caches by
  the content digest of each step's inputs (Chapter 10 §4 dedup).

## Connection to containers

- The image you built runs through the exact pipeline of Lab 01: unpack layers →
  overlay rootfs → OCI bundle → runc → kernel. Build and run are two sides of the
  same layer/overlay/content-addressing model (Chapters 08, 10).

## Questions to think about

1. Reorder the Dockerfile so `COPY hello.txt` comes before `RUN`. How does that
   change which steps rebuild when `hello.txt` changes? Why?
2. Rewrite this as a `FROM scratch` image with a single static binary. What would
   the rootfs contain, and why does it need nothing else? (Chapter 07 §1.)
3. Two images `FROM alpine` share the base layer. Using `docker history` and
   `RootFS.Layers`, how would you prove they share it? (Chapter 10 §4.)
