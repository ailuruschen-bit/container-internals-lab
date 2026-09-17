# 3. Images and Build (BuildKit)

This section closes the image side: how `docker build` turns a Dockerfile into the
OCI image layers of Chapters 08 and 10 §4. Building is not required to
*understand* running a container, so this is a shorter section, but it completes
the picture of where images come from.

## A Dockerfile is a recipe for layers

Each instruction that changes the filesystem produces (roughly) one **layer** — a
diff over the previous one (Chapter 08 §1, §3):

```dockerfile
FROM debian:12          # the base image's layers become the lower layers
RUN apt-get install -y nginx    # a layer: the filesystem diff of the install
COPY nginx.conf /etc/nginx/     # a layer: the added file
CMD ["nginx","-g","daemon off;"] # metadata only (no filesystem layer) → image config
```

- `FROM`, `RUN`, `COPY`, `ADD` produce filesystem layers.
- `ENV`, `CMD`, `ENTRYPOINT`, `USER`, `WORKDIR`, `EXPOSE` set fields in the
  **image config** (Chapter 10 §4), which later become `config.json` process
  fields (Chapter 10 §2, §4 step 5).

The result is exactly the OCI image you inspected in Chapter 10 Lab 02: a
manifest referencing a config and ordered layer blobs, content-addressed.

## BuildKit: how the build runs

Modern Docker builds use **BuildKit** (the default builder). Conceptually:

```text
Dockerfile → BuildKit frontend → a build graph (LLB)
for each layer-producing step:
   run the step's command in a CONTAINER over the current layers   (Ch. 08 §2 overlay)
   snapshot the resulting filesystem diff as a new layer            (Ch. 08 §3)
   compute its digest; cache by (inputs → digest)                   (Ch. 10 §4)
assemble the manifest + config; store in the content store          (Ch. 10 §4, Ch. 12 §3)
```

Key points that connect to earlier chapters:

- Each `RUN` executes **inside a container** (namespaces, overlay rootfs) — the
  builder uses the same runtime primitives to build images. Building containers
  needs containers.
- Layers are **cached by content**: if a step's inputs are unchanged, its cached
  layer digest is reused (Chapter 10 §4 dedup), which is why rebuilds are fast and
  why instruction order matters for cache hits.
- The output is an OCI image; nothing about it is Docker-specific, so it runs
  under any OCI runtime (Chapter 10 §1).

## Multi-stage builds and `scratch`

- **Multi-stage** builds use one stage to compile and copy only the artifact into
  a smaller final stage, producing fewer/smaller layers.
- `FROM scratch` starts from an **empty** rootfs (Chapter 07 §1): a static binary
  plus nothing else. This is the smallest image and directly uses the
  "static binary needs no libraries" fact from Chapter 07 Lab 01.

## Why this matters

- It shows that the image you *run* (Chapters 08–12) is produced by the same
  layer/overlay/content-addressing mechanisms, so "build" and "run" are two sides
  of the same model.
- It explains build-cache behavior and `FROM scratch` in terms you already
  understand (Chapters 07, 08, 10).

## Evidence

Lab: [`lab-03-build-an-image`](../../labs/13-moby-docker-engine/lab-03-build-an-image/)

## Further Reading

- Docker: [Dockerfile reference](https://docs.docker.com/reference/dockerfile/)
  and [Build with BuildKit](https://docs.docker.com/build/buildkit/).
- [moby/buildkit](https://github.com/moby/buildkit) — the builder; the
  [LLB](https://github.com/moby/buildkit#llb) concept.
- OCI Image Spec [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md)
  and [config.md](https://github.com/opencontainers/image-spec/blob/main/config.md)
  — what a build produces (Chapter 10 §4).
- Chapters 07 §1 (rootfs, scratch), 08 (layers), 10 §4 (image graph).
