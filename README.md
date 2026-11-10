# Technical Learning Notes

**English** | [中文](README.zh.md) | [日本語](README.ja.md)

This repository records the small questions, small experiments, and evolving understanding that genuinely struck me while learning.

It does not try to build a complete body of knowledge in one go. Each note explains one question clearly, as I understood it at the time, and is revised as my understanding changes.

## Notes

- 01. [Who Owns an Environment Variable?](notes/01-environment-variables-belong-to-processes.md)
- 02. [How Does the Operating System Take Back the CPU?](notes/02-how-the-kernel-takes-back-the-cpu.md)
- 03. [Linux Filesystems, Mounts, and Mount Namespaces: What I Learned](notes/03-where-is-container-root-filesystem.md)
- 04. [Namespaces Isolate the View; Cgroups Limit the Usage](notes/04-namespace-views-and-cgroup-limits.md)
- 05. [How Linux Container Networking Works: From Network Namespaces to NAT](notes/05-linux-container-networking.md)
- 06. [Hands-On Docker Basics: Mapping Mounts and Networks to Kernel Mechanisms](notes/06-docker-mount-and-network-practice.md)
- 07. [Docker Image Basics: Learning to Inspect Images First](notes/07-docker-image-basics.md)
- 08. [Why Do I Need Containerization?](notes/08-why-containerize-applications.md)
- 09. [Building Docker Images in Practice (1): Hello World](notes/09-docker-image-build-hello-world.md)
- 10. [Building Docker Images in Practice (2): Customizing the Nginx Home Page](notes/10-docker-image-build-nginx.md)
- 11. [Building Docker Images in Practice (3): Cache, ARG/ENV, and .dockerignore](notes/11-docker-image-build-cache-args-ignore.md)
- 12. [Building Docker Images in Practice (4): Java and Multi-stage Builds](notes/12-docker-image-build-java-multistage.md)
- 13. [Docker Images in Practice (5): Version Replacement, Rollback, and Registries](notes/13-docker-image-version-rollback-registry.md)
- 14. [Getting Started with Docker Compose: From `docker run` to Multi-Service Configuration](notes/14-docker-compose-basics.md)
- 15. [Docker `USER`: Running Applications as a Non-root User](notes/15-docker-user-best-practices.md)

English and Japanese versions are added progressively as new notes are written.

## Labs

The Dockerfiles, sources, and Compose files used in the notes are in [`labs/`](labs/), with comments explaining each step.

## Archived Content

The earlier articles and labs on container internals, organized by chapter, are preserved in full on the `archive/structured-container-course-2026-10-18` branch.
