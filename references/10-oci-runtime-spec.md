# References — Chapter 10: The OCI Specifications

Version note: describes OCI Runtime Spec **v1.2.x**, Image Spec **v1.1.x**, and
Distribution Spec **v1.1.x** (current as of writing). Field names and defaults
can change between spec versions; each link points to `main`, so check the tag
your tools target.

## OCI specifications (primary)

| Document | Why read it | Used in |
|---|---|---|
| [runtime-spec/config.md](https://github.com/opencontainers/runtime-spec/blob/main/config.md) | process, root, mounts, hooks, user, annotations. | §2, §3 |
| [runtime-spec/config-linux.md](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md) | namespaces, cgroups (`resources`), devices, seccomp, masked/readonly paths, propagation, default devices/filesystems. | §2 |
| [runtime-spec/runtime.md](https://github.com/opencontainers/runtime-spec/blob/main/runtime.md) | the state machine and the create/start/kill/delete/state operations. | §3 |
| [image-spec/spec.md](https://github.com/opencontainers/image-spec/blob/main/spec.md), [manifest.md](https://github.com/opencontainers/image-spec/blob/main/manifest.md), [image-index.md](https://github.com/opencontainers/image-spec/blob/main/image-index.md), [config.md](https://github.com/opencontainers/image-spec/blob/main/config.md), [layer.md](https://github.com/opencontainers/image-spec/blob/main/layer.md), [descriptor.md](https://github.com/opencontainers/image-spec/blob/main/descriptor.md) | the image graph, config runtime metadata, layer/whiteout format, digests. | §4 |
| [distribution-spec/spec.md](https://github.com/opencontainers/distribution-spec/blob/main/spec.md) | the registry pull/push HTTP API. | §4 |

## Tools (primary for the labs)

- [runc man pages](https://github.com/opencontainers/runc/tree/main/man) —
  `runc-spec`, `runc-create`, `runc-start`, `runc-state`, `runc-kill`,
  `runc-delete`, `runc-run`.
- [`skopeo`](https://github.com/containers/skopeo) and
  [`crane`](https://github.com/google/go-containerregistry/tree/main/cmd/crane)
  — inspect manifests, configs, and layers without a daemon.
- [`jq`](https://jqlang.github.io/jq/) — read `config.json` and image JSON.

## Related standards (for connections)

- CNI: [SPEC.md](https://github.com/containernetworking/cni/blob/main/SPEC.md) —
  invoked via OCI hooks to wire the network namespace (§3, Ch. 03 §6).
- CRI (Kubernetes): [cri-api](https://github.com/kubernetes/cri-api) — the
  Kubernetes↔runtime interface, above the OCI (Chapter 12).

## Background

- OCI: [opencontainers.org overview](https://opencontainers.org/about/overview/).
- Docker blog, [Open Container Project](https://www.docker.com/blog/open-container-project-foundation/)
  (2015) — why the OCI was formed.
