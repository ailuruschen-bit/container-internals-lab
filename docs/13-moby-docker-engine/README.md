# Chapter 13 — Moby / Docker Engine

## Why this chapter exists

This is the top of the stack and the destination of the whole repository. The
**Docker Engine** (whose open-source project is **Moby**) is what most people
mean by "Docker": the daemon `dockerd`, the `docker` CLI, image **building**,
**networking**, and **volumes**. Underneath, it uses **containerd** (Chapter 12),
which uses **runc** (Chapter 11), which uses the **Linux mechanisms**
(Chapters 01–08). This chapter maps that top layer and then answers, one final
time and in full, the question the repository opened with:

> What actually happens when I run `docker run nginx`?

We do not read all of Moby (it is huge); we map its responsibilities and trace
the one command, connecting every step to an earlier chapter.

## Reading order

| # | Section | Core idea |
|---|---|---|
| 1 | [The Docker architecture](01-architecture.md) | CLI → dockerd → containerd → runc; what Moby adds. |
| 2 | [Networking: libnetwork](02-networking.md) | bridge, veth, port publishing (Chapter 03 §6), CNM. |
| 3 | [Images and build (BuildKit)](03-build.md) | building layers; how `docker build` produces OCI images. |
| 4 | [`docker run nginx`, end to end](04-docker-run-nginx.md) | the capstone trace through every layer. |
| 5 | [Chapter summary and the whole stack](05-summary.md) | the complete mental model. |

Labs: [`labs/13-moby-docker-engine/`](../../labs/13-moby-docker-engine/).
References: [`references/13-moby-docker-engine.md`](../../references/13-moby-docker-engine.md).

## Prerequisites

- **Chapters 01–12.** This chapter is a synthesis and assumes all of them.

## Versions

Written against **Docker Engine / Moby 27.x** (current as of writing), which uses
containerd and runc underneath. Internals change; the prompt's rule applies.
Links point to `moby/moby` `master`; check `docker version`.

## Environment

A Linux VM with Docker installed and running, `sudo`, `jq`, `strace`, and the
tools from Chapters 03/12 (`ip`, `nft`/`iptables`, `ctr`).
