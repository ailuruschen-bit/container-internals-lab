# 1. Why the OCI Exists

## The problem: fragmentation

By 2015, "containers" meant several incompatible things. Docker had its own image
format and runtime; CoreOS's rkt had another (appc). An image built for one might
not run on the other; a tool that started containers had to target a specific
engine. For an ecosystem (registries, orchestrators, security scanners, build
tools) this fragmentation was untenable.

The **Open Container Initiative** was formed in 2015 (under the Linux Foundation,
with Docker donating the `runc` codebase as a seed) to define **vendor-neutral
standards** so that any conforming image runs under any conforming runtime, and
any tool can produce or consume them.

## The three specifications

| Specification | Answers | Chapter connection |
|---|---|---|
| **Runtime Specification** | Given a filesystem and a `config.json`, how do you *run* a container? What is in the config? What is its lifecycle? | Chapters 01–09 (the mechanisms); Chapter 11 (runc implements it) |
| **Image Specification** | How is a container image structured, addressed, and turned into a runnable filesystem + config? | Chapter 08 (layers); Chapter 12 (containerd consumes it) |
| **Distribution Specification** | How are images pushed to and pulled from a registry (the HTTP API)? | Chapter 12 (pull) |

They fit together in a pipeline:

```text
registry ──(Distribution spec: pull)──► image (Image spec)
   ──(unpack layers + translate config)──► OCI bundle = rootfs/ + config.json (Runtime spec)
   ──(runc create/start)──► running container (Chapters 01-08 mechanisms)
```

## What "conformance" buys you

- **Portability:** an image from any builder runs under runc, crun, youki,
  gVisor's runsc, or Kata, because they all read the same `config.json` and
  rootfs.
- **Interchangeable layers:** high-level (containerd, CRI-O, Docker) and
  low-level (runc and friends) components are decoupled by the runtime spec's
  bundle + CLI contract. Kubernetes can use containerd or CRI-O; containerd can
  use runc or Kata; you can swap either side.
- **Reuse:** registries, signing (cosign), SBOM tools, and scanners all target
  the image spec.

## What the OCI is NOT

- It is not a runtime or an engine; it is documents (plus a small conformance
  test suite and reference code).
- It does not standardize **networking** (that is CNI, Chapter 03 §6) or the
  **Kubernetes runtime interface** (that is CRI, Chapter 12). The runtime spec
  deliberately leaves the network namespace's *contents* to the caller.
- It does not define higher-level concepts like "pods" or "services".

## Why this matters

Understanding that the OCI is a **description layer** over the Linux mechanisms
you already know is the key to reading the rest of the stack. `config.json` will
look familiar because it is `minic`'s flags, formalized; the lifecycle will look
familiar because it is create-configure-execute, formalized.

## Further Reading

- OCI: [opencontainers.org](https://opencontainers.org/) and the
  [About the OCI](https://opencontainers.org/about/overview/) overview.
- [runtime-spec](https://github.com/opencontainers/runtime-spec),
  [image-spec](https://github.com/opencontainers/image-spec),
  [distribution-spec](https://github.com/opencontainers/distribution-spec) — the
  three repositories; read each `spec.md` and `README.md`.
- [Open Container Initiative announcement](https://www.docker.com/blog/open-container-project-foundation/)
  (2015) — the historical motivation, for context.
