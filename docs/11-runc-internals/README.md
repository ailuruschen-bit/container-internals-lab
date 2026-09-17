# Chapter 11 — runc Internals

## Why this chapter exists

`minic` (Chapter 09) showed the mechanisms; the OCI (Chapter 10) standardized the
description. **runc** is the production program that reads an OCI bundle and
applies the mechanisms correctly and securely. It is the reference OCI runtime,
donated by Docker to seed the OCI, and it (or a compatible runtime like crun or
youki) sits under Docker, containerd, CRI-O, and Kubernetes.

This chapter is a **guided source-code reading**. We do not explain the whole
repository; we trace one path, `runc run`, and see how it configures namespaces,
rootfs, cgroups, capabilities, and seccomp, then `execve`s. Every step links back
to the experiment in Chapters 01–09 that reproduces it, so the code confirms what
you already understand rather than introducing new ideas.

## How to read this chapter

Open the runc source alongside it (links are given per section). **Do not read
files top to bottom.** Follow the call chain: from the CLI command, to the
function that builds the child process, to the C bootstrap, to the Go init that
finishes setup, to `execve`. Each section names the file, the struct, and the
function, gives simplified pseudocode, and connects it to a Linux mechanism.

## Reading order

| # | Section | Traces |
|---|---|---|
| 1 | [Layout and the run path](01-layout-and-run-path.md) | the repo structure; `runc run` → libcontainer → a re-exec of `runc init` |
| 2 | [nsexec: the C bootstrap](02-nsexec.md) | why C, the three-process dance, namespaces and uid/gid maps |
| 3 | [The container init: rootfs](03-init-rootfs.md) | `prepareRootfs`, mounts, `pivotRoot`, masked/readonly paths |
| 4 | [Finalizing: cgroups, caps, seccomp, execve](04-finalize.md) | the cgroup manager, capabilities, seccomp, the final exec |
| 5 | [Two real vulnerabilities](05-vulnerabilities.md) | CVE-2019-5736 and CVE-2024-21626, explained with earlier chapters |
| 6 | [Chapter summary](06-summary.md) | the whole path, and how to keep reading |

Labs: [`labs/11-runc-internals/`](../../labs/11-runc-internals/).
References: [`references/11-runc-internals.md`](../../references/11-runc-internals.md).

## Prerequisites

- **Chapters 01–10.** This chapter assumes fluency with every mechanism.
- Basic Go reading ability (you do not need to write Go). A little C for §2.

## Versions

runc's internals change between releases, and the prompt's rule applies: this
chapter is written against **runc v1.2.x** (the series current as of writing).
File and function names below are stable across that series, but **line numbers
are not**, so we cite functions and structs, not lines. Check your version with
`runc --version` and read the matching tag. Links point to the `main` branch;
where a detail is version-sensitive it is flagged.
