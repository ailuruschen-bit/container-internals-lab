# 4. The Image Specification

## From "a rootfs" to "a distributable image"

Chapter 08 showed that a rootfs is a stack of layers. The **OCI Image
Specification** formalizes how those layers and their metadata are packaged,
addressed, and turned into a runnable bundle. It is what `docker pull`,
`docker build`, registries, and containerd all agree on.

## Content addressing: the digest

Every object in an image is identified by the **digest** of its bytes:
`sha256:<hex>`. This is the foundation:

- the same content always has the same digest, so identical layers are stored and
  transferred **once** (Chapter 08 §1);
- a digest is **immutable and verifiable**: pulling by digest guarantees you got
  exactly those bytes (the basis of image signing and reproducibility);
- objects reference each other by digest, forming a Merkle DAG.

## The objects

```text
image index (optional)        one image, multiple platforms (linux/amd64, linux/arm64, ...)
      │ references by digest + platform
      ▼
manifest                      one image for one platform
      ├── config  (digest) ─► image config JSON
      └── layers[] (digests) ─► layer blobs (tar+gzip), ordered lowest→highest
```

| Object | Media type (roughly) | Contents |
|---|---|---|
| **Image index** | `...image.index.v1+json` | a list of manifests by platform (multi-arch images) |
| **Manifest** | `...image.manifest.v1+json` | references to one config and the ordered layers |
| **Config** | `...image.config.v1+json` | how to run: env, entrypoint, cmd, working dir, user, exposed ports, **and** `rootfs.diff_ids` and a `history` |
| **Layer** | `...image.layer.v1.tar+gzip` | a tar of the filesystem diff (Chapter 08 §3), with `.wh.` whiteouts |

### The config is where runtime metadata lives

The image **config** carries the defaults that become part of the OCI **runtime**
`config.json` when a container is created:

```json
{
  "architecture": "amd64", "os": "linux",
  "config": {
    "Env": ["PATH=/usr/bin:/bin"],
    "Entrypoint": ["/entrypoint.sh"],
    "Cmd": ["--serve"],
    "WorkingDir": "/app",
    "User": "1000:1000",
    "ExposedPorts": {"8080/tcp": {}}
  },
  "rootfs": { "type": "layers", "diff_ids": ["sha256:...","sha256:..."] },
  "history": [ {"created_by":"RUN apt-get ..."}, ... ]
}
```

Note the two identifiers per layer:

- **`diff_id`**: the digest of the **uncompressed** tar (the filesystem content),
  listed in the config's `rootfs.diff_ids`. Used to compute the chain of layers.
- the **layer digest** in the manifest: the digest of the **compressed** blob as
  stored/transferred.

## How an image becomes a runnable bundle

Combining Chapter 08 §3 and Chapter 10 §2, a runtime (or containerd) turns an
image into a bundle:

```text
1. resolve index → manifest for this platform (by digest)
2. pull the config and layer blobs into the content store (by digest)
3. unpack layers in order into snapshots, applying .wh. whiteouts   (Ch. 08 §3)
4. overlay-mount the snapshots → the rootfs                          (Ch. 08 §2)
5. translate the image config (Env, Entrypoint+Cmd, User, WorkingDir)
   into config.json's process fields                                 (Ch. 10 §2)
6. add the runtime's defaults (namespaces, mounts, caps, seccomp, ...)
   → a complete OCI bundle → runc create/start                       (Ch. 10 §2-3)
```

Step 5 is where `ENTRYPOINT`/`CMD` become `process.args`, `ENV` becomes
`process.env`, `USER` becomes `process.user`, and `WORKDIR` becomes
`process.cwd`. The image spec and runtime spec meet here.

## Distribution, briefly

The **Distribution Specification** is the HTTP API a registry exposes to push and
pull these objects by digest and tag (`GET /v2/<name>/manifests/<ref>`,
`GET /v2/<name>/blobs/<digest>`, and the upload counterparts). A **tag**
(`nginx:1.27`) is a human-friendly name that resolves to a manifest (or index)
digest; the digest is the truth. Chapter 12 covers the pull path in containerd.

## Why this matters

- It explains why `docker pull` of a shared base is fast (digests dedupe), why
  images are immutable and signable (content addressing), and how a registry
  artifact becomes the rootfs + config.json that Chapters 07–09 assumed.
- When you read containerd (Chapter 12), the content store, manifests, configs,
  and diff_ids are exactly these objects.

## Evidence

Lab: [`lab-02-inspect-an-image`](../../labs/10-oci-runtime-spec/lab-02-inspect-an-image/)

## Further Reading

- OCI Image Spec: [spec.md](https://github.com/opencontainers/image-spec/blob/main/spec.md),
  [manifest.md](https://github.com/opencontainers/image-spec/blob/main/manifest.md),
  [image-index.md](https://github.com/opencontainers/image-spec/blob/main/image-index.md),
  [config.md](https://github.com/opencontainers/image-spec/blob/main/config.md),
  [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md),
  [descriptor.md](https://github.com/opencontainers/image-spec/blob/main/descriptor.md)
  (digests).
- OCI Distribution Spec: [spec.md](https://github.com/opencontainers/distribution-spec/blob/main/spec.md).
- [`skopeo`](https://github.com/containers/skopeo) and
  [`crane`](https://github.com/google/go-containerregistry/tree/main/cmd/crane)
  — tools to inspect manifests, configs, and layers without a daemon (used in the
  lab).
