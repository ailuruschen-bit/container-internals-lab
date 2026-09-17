# 6. CRI: containerd Under Kubernetes

## The problem Kubernetes has

Kubernetes must run containers, but it should not be tied to one container
runtime. So the kubelet talks to a runtime through the **Container Runtime
Interface (CRI)**, a gRPC API. Any runtime that implements CRI can back
Kubernetes: containerd (via its built-in CRI plugin) and CRI-O are the common
ones. (Docker's own API was supported through a shim called `dockershim`, removed
from Kubernetes in 1.24; Docker-built images still run fine, because they are OCI
images.)

```text
kubelet ──CRI (gRPC)──► containerd CRI plugin ──► containerd services ──► shim ──► runc
```

The CRI plugin is a service **inside** containerd; it translates CRI calls into
the content/snapshots/containers/tasks operations of §§2–5.

## Pods: the sandbox and the shared namespaces

CRI's unit is the **pod**, not the container. A pod is a group of containers that
share some Linux namespaces (Chapter 03). containerd implements this with a
**pod sandbox**:

```text
RunPodSandbox:
  create a "pause" container (a tiny process that just sleeps)
  it OWNS the pod's shared namespaces: network, IPC, (optionally PID), UTS   (Ch. 03)
  its network namespace is set up by a CNI plugin via hooks                  (Ch. 03 §6, Ch. 10 §3)
CreateContainer / StartContainer (per app container):
  create a container that JOINS the sandbox's namespaces (setns/config path) (Ch. 03 §1)
  each app container has its OWN mount namespace + rootfs (its image)        (Ch. 03 §4, Ch. 08)
  but shares the sandbox's network/IPC → they reach each other on localhost  (Ch. 03 §6)
```

The **pause container** exists precisely to hold the namespaces open
independently of any app container, so an app container can restart without the
pod losing its network namespace and IP (Chapter 03 §1's "keep a namespace alive"
— here via a live process rather than a bind mount). It is also the pod's PID 1
when the pod shares a PID namespace, reaping zombies (Chapter 01 §2, Chapter 03
§3).

## Mapping pod spec to mechanisms

| Pod/container spec | Mechanism | Chapter |
|---|---|---|
| pod shares one network namespace | `CLONE_NEWNET` on the sandbox; app containers `setns` | 03 §6, §1 |
| `shareProcessNamespace: true` | one PID namespace across the pod | 03 §3 |
| `resources.limits/requests` | cgroup v2 files per container and per pod | 04 §7 |
| `securityContext.capabilities` | OCI capabilities | 05 |
| `securityContext.seccompProfile` | OCI seccomp | 06 |
| `runAsNonRoot`, `allowPrivilegeEscalation:false` | user/`no_new_privs` | 05, 06 §1 |
| `volumeMounts`, ConfigMaps, Secrets | bind mounts | 02 §3 |
| pod networking, IP | CNI plugin via hooks on the sandbox netns | 03 §6, 10 §3 |

Everything a pod spec configures is one of the mechanisms from Chapters 01–08,
applied through the OCI spec (Chapter 10) by runc (Chapter 11), orchestrated by
containerd (this chapter). Kubernetes adds scheduling and policy, not new kernel
mechanisms.

## Scope note

This repository is not a Kubernetes course (see the root README). CRI is included
only to show that Kubernetes sits on the same stack: it is another client of
containerd, and a pod is a namespace-sharing arrangement you already understand.

## Why this matters

- It closes the "how does Kubernetes actually run a container" question with the
  mechanisms you know: a pause container holding shared namespaces, app
  containers joining them, CNI wiring the network, and runc doing the per-
  container isolation.
- It shows that Docker and Kubernetes are two clients of the **same** containerd →
  shim → runc → kernel path (§5).

## Further Reading

- Kubernetes: [Container Runtime Interface (CRI)](https://kubernetes.io/docs/concepts/architecture/cri/)
  and [Container Runtimes](https://kubernetes.io/docs/setup/production-environment/container-runtimes/).
- Kubernetes blog: [Don't Panic: Kubernetes and Docker](https://kubernetes.io/blog/2020/12/02/dont-panic-kubernetes-and-docker/)
  — the `dockershim` removal, explained.
- containerd [CRI plugin docs](https://github.com/containerd/containerd/blob/main/docs/cri/README.md).
- The "pause" container: [pause source](https://github.com/kubernetes/kubernetes/tree/master/build/pause)
  — how small the namespace-holder really is.
- Chapters 03 (namespaces), 03 §6 (CNI), 10 §3 (hooks).
