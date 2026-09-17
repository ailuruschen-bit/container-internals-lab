# Chapter 10 — The OCI Specifications

## Why this chapter exists

In Chapter 09 you configured a container with ad-hoc flags. Every real tool needs
to describe the *same* configuration, and different tools must agree so that an
image built by one runs under another. The **Open Container Initiative (OCI)**
publishes the specifications that make this possible. This chapter shows how the
things `minic` did map onto standard, tool-independent formats, so that when you
open runc (Chapter 11) or containerd (Chapter 12) you recognize the vocabulary.

The key realization: **the OCI specs do not add new mechanisms.** They are a
precise, versioned description of the Linux primitives from Chapters 01–08. A
`config.json` is a serialization of the decisions `minic` made in code.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [Why the OCI exists](01-why-oci.md) | Standardizing images and runtimes; the three specs. |
| 2 | [The runtime spec: config.json](02-runtime-spec-config.md) | Every section mapped to a Linux mechanism you know. |
| 3 | [The runtime lifecycle](03-runtime-lifecycle.md) | create/start/kill/delete, state, hooks; the bundle. |
| 4 | [The image spec](04-image-spec.md) | Manifests, config, layers, digests; how an image becomes a bundle. |
| 5 | [Chapter summary](05-summary.md) | What changed, what did not. |

Labs: [`labs/10-oci-runtime-spec/`](../../labs/10-oci-runtime-spec/).
References: [`references/10-oci-runtime-spec.md`](../../references/10-oci-runtime-spec.md).

## Prerequisites

- **Chapters 01–09.** This chapter maps specs onto those mechanisms and does not
  re-explain them.

## Versions

This chapter describes the OCI Runtime Specification **v1.2.x** and the Image
Specification **v1.1.x** (the versions current as of writing). The specs evolve;
each concept below links to the versioned document, and where a field is
version-dependent it is noted. Always check the spec version your runtime
implements (`runc --version` prints the spec version it targets).

## Environment

A Linux VM with `runc` installed (`apt-get install runc` or from the OCI
releases), plus `jq`. Docker or containerd optional for the image-inspection lab.
