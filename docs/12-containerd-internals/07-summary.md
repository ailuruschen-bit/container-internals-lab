# 7. Chapter Summary: The High-Level Runtime

## The model you should now have

containerd is the **high-level runtime**: a daemon of services that does
everything around running a container, and delegates the one-container kernel work
to a **low-level runtime** (runc) through a **shim**.

```text
client (dockerd / kubelet / ctr) ──gRPC──► containerd daemon
   Content   : image blobs by digest                 (Ch. 10 §4)
   Snapshots : layers → committed + active snapshots  (Ch. 08)
   Images    : name → manifest digest                 (Ch. 10 §4)
   Containers: OCI spec + snapshot + runtime          (Ch. 10 §2)
   Tasks     : the running process, via a shim
        └── containerd-shim-runc-v2 (per container, subreaper, owns stdio)  (Ch. 01 §2)
              └── runc (applies the bundle once, then exits)                (Ch. 11)
                    └── container process                                    (Ch. 01-08)
```

## Key ideas

| Idea | Why | Chapter |
|---|---|---|
| high- vs low-level runtime split | swap runc↔crun↔Kata↔runsc; swap containerd↔CRI-O | 12 §1, 10 |
| content store (digests) | dedup, integrity, immutability | 10 §4 |
| snapshotters (overlayfs) | layers → shared read-only + private writable rootfs | 08 |
| Container vs Task | static config vs running process | 12 §4, 10 §3 |
| the shim | container survives daemon restart; reaps; owns stdio; runc exits | 01 §2, 12 §4 |
| CRI + pod sandbox (pause) | Kubernetes on the same stack; shared namespaces | 03, 12 §6 |

## The sentence to remember

> containerd manages images, storage, and the container lifecycle and exposes an
> API; a per-container shim supervises the process so it outlives the daemon;
> runc does the kernel work once. Docker and Kubernetes are both clients of this
> same path.

## Self-check questions

1. Name three jobs containerd does that runc does not, and the chapter each
   relates to. (§1)
2. Where do an image's bytes live, and how do they become a mountable rootfs?
   Name the services and the Chapter 08 mechanism. (§2, §3)
3. What is the difference between a Container and a Task? (§4)
4. Give four reasons the shim exists, each tied to Chapter 01 §2. (§4)
5. On a host running containers, why is `containerd-shim-runc-v2`, not
   `containerd`, the parent of the container process? (§4)
6. Trace `ctr run alpine demo /bin/sh` through the services to a running task.
   (§5)
7. What is the pause container for, and how does a pod share `localhost` between
   containers? (§6, Ch. 03)

## Next: Chapter 13 — Moby / Docker Engine

One layer remains: the Docker Engine (Moby), which sits above containerd and adds
the developer-facing pieces — the Docker API, image **building**, **networking**
(libnetwork), and **volumes**. Chapter 13 traces `docker run nginx` from the CLI
all the way down through everything you have learned, and revisits, one final
time, the question this repository started with: *what actually happens when I run
`docker run nginx`?*
