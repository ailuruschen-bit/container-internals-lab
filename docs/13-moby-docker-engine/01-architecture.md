# 1. The Docker Architecture

## The pieces

"Docker" is several programs, not one:

```text
docker (CLI)  ──HTTP/REST over /var/run/docker.sock──►  dockerd (the Engine daemon)
                                                          │ gRPC
                                                          ▼
                                                     containerd (Ch. 12)
                                                          │  shim per container
                                                          ▼
                                          containerd-shim-runc-v2 → runc (Ch. 11)
                                                          ▼
                                                   Linux kernel (Ch. 01-08)
```

| Component | Role |
|---|---|
| **`docker` CLI** | a client that turns your command into an HTTP request to the Engine API |
| **`dockerd`** | the Docker Engine daemon: the Engine API, image management, **build**, **networking**, **volumes**; delegates container execution to containerd |
| **containerd** | the high-level runtime: images, snapshots, tasks, the shim (Chapter 12) |
| **runc** | the low-level runtime: applies one OCI bundle (Chapter 11) |

`dockerd` and `containerd` are separate daemons. `dockerd` is a **client of
containerd**, using containerd's namespace `moby` for its objects (you saw this in
Chapter 12 Lab 01 with `ctr -n moby`).

## What Moby adds on top of containerd

containerd already does images, storage, tasks, and supervision. So what is
`dockerd` for? The developer-facing layer:

| Moby responsibility | Notes | Chapter it builds on |
|---|---|---|
| **The Engine API** | the stable REST API the `docker` CLI (and many tools) use | — |
| **Image build** | `docker build` / BuildKit turns a Dockerfile into OCI image layers | 08, 10 §4 |
| **Networking** | libnetwork: the default `bridge` network (`docker0`), veth pairs, port publishing (DNAT) | 03 §6 |
| **Volumes** | named volumes and bind mounts, mounted into the container | 02 §3 |
| **Higher-level UX** | `docker ps`, logs, restart policies, healthchecks, compose integration | 01 §2 (logs via shim), 12 §4 |

None of these are new kernel mechanisms; they are management and developer
experience over the mechanisms of Chapters 01–08, applied through containerd and
runc.

## The request flow

`docker run nginx` becomes, at the top:

```text
docker CLI: POST /containers/create  then  POST /containers/{id}/start   (Engine API)
dockerd:
   - ensure the image is present (pull via containerd if needed)          Ch. 12 §3
   - build the container config: merge image config + CLI flags           Ch. 10 §2, §4
   - set up networking (libnetwork): create the endpoint on docker0       §2, Ch. 03 §6
   - set up volumes/mounts                                                Ch. 02 §3
   - call containerd: create container + task                             Ch. 12 §4-5
containerd → shim → runc → kernel                                          Ch. 11, 01-08
```

Chapter 13 §4 expands this into the full capstone trace.

## Historical note: dockerd, containerd, and runc

Docker originally did everything in one binary. Over 2015–2017 it was factored
into layers: `runc` (donated to seed the OCI, Chapter 10 §1), then `containerd`
(donated to the CNCF, Chapter 12 §1), leaving `dockerd`/Moby as the top layer.
This is why the stack is so cleanly separable, and why Kubernetes could later use
containerd directly and drop the Docker-specific shim (Chapter 12 §6). Knowing
this history explains the architecture's shape.

## Why this matters

- It resolves "Docker vs containerd vs runc" definitively: they are cooperating
  layers, and `dockerd` is the developer-facing top that adds build, networking,
  and volumes over containerd.
- It sets the scope for the rest of the chapter: only three Moby-specific topics
  remain (networking §2, build §3), and then the full trace (§4).

## Evidence

Lab: [`lab-01-docker-layers`](../../labs/13-moby-docker-engine/lab-01-docker-layers/)

## Further Reading

- Docker: [Docker Engine architecture / overview](https://docs.docker.com/get-started/docker-overview/)
  and the [Engine API reference](https://docs.docker.com/reference/api/engine/).
- Moby: [moby/moby](https://github.com/moby/moby) README and
  [architecture](https://github.com/moby/moby/tree/master/docs).
- Docker blog, ["What is containerd?"](https://www.docker.com/blog/what-is-containerd-runtime/)
  and the runc/containerd donation history (Chapter 10 §1, 12 §1).
