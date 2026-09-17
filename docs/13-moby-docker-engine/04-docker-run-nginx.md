# 4. `docker run nginx`, End to End

This is the capstone of the repository. We trace `docker run -d -p 8080:80 nginx`
through **every layer**, and for each step name the chapter that explains it. By
now none of it should be mysterious: it is the mechanisms of Chapters 01–08,
applied by runc (11), orchestrated by containerd (12), managed by Moby (13).

## The full trace

```text
$ docker run -d -p 8080:80 nginx
```

### Layer 1 — the CLI (Chapter 13 §1)

```text
docker CLI parses the command and sends two HTTP requests to dockerd over
/var/run/docker.sock:
   POST /images/create?fromImage=nginx      (if not present locally)
   POST /containers/create  { Image: "nginx", HostConfig: { PortBindings: 8080→80 } }
   POST /containers/{id}/start
```

The CLI does no container work; it is a REST client of the Engine API.

### Layer 2 — dockerd (Chapter 13 §1–2)

```text
dockerd:
 a. resolve "nginx" → docker.io/library/nginx:latest; ask containerd to pull
    if absent: manifest+config+layers into the content store; unpack to snapshots
                                                       (Ch. 12 §3; Ch. 10 §4; Ch. 08 §3)
 b. build the container config: merge the IMAGE config (Entrypoint=nginx -g
    'daemon off;', Env, User, WorkingDir, ExposedPorts) with the CLI flags
                                                       (Ch. 10 §2, §4)
 c. networking (libnetwork): create a sandbox (netns), a veth pair to docker0,
    assign 172.17.0.x, add a DNAT rule host:8080 → 172.17.0.x:80
                                                       (Ch. 13 §2; Ch. 03 §6-7)
 d. volumes/mounts: bind-mount /etc/hosts, /etc/hostname, /etc/resolv.conf, any -v
                                                       (Ch. 02 §3)
 e. hand off to containerd: create the container + task, passing the OCI spec and
    the prepared network namespace path                (Ch. 12 §4-5; Ch. 10 §3 hooks)
```

### Layer 3 — containerd (Chapter 12 §4–5)

```text
containerd (namespace "moby"):
 - Containers service records the OCI spec + snapshot + runtime
 - Snapshots.Prepare: an active (writable) overlay snapshot for this container
                                                       (Ch. 08 §2; Ch. 12 §3)
 - Tasks service starts containerd-shim-runc-v2 for this container
 - the shim assembles the OCI bundle (config.json + rootfs from the overlay mounts)
                                                       (Ch. 10 §2; Ch. 08 §2)
 - shim → runc create, then (on start) runc start; shim supervises, owns stdio
                                                       (Ch. 12 §4; Ch. 01 §2)
```

### Layer 4 — runc (Chapter 11)

```text
runc create/start:
 - nsexec (C): create the namespaces (mount, PID, UTS, IPC, cgroup; net is JOINED
   via the path libnetwork prepared), write uid/gid maps if userns  (Ch. 11 §2; Ch. 03)
 - Go init: mount /proc,/sys,/dev; pivot_root into the overlay rootfs; detach old
   root; apply masked/readonly paths                    (Ch. 11 §3; Ch. 02, 07)
 - apply the cgroup (parent side): memory.max, cpu, pids           (Ch. 11 §4; Ch. 04)
 - setup user; apply capabilities (drop most); no_new_privs         (Ch. 11 §4; Ch. 05, 06)
 - wait on the exec FIFO (state: created) → runc start writes it    (Ch. 10 §3)
 - install seccomp (the default profile)                            (Ch. 11 §4; Ch. 06)
 - execve("/docker-entrypoint.sh" → nginx)                          (Ch. 01 §3)
```

### Layer 5 — the kernel (Chapters 01–08)

```text
The nginx process now runs as a Linux process (Ch. 01) that the kernel gives:
 - its own PID space (it is PID 1 in the container), UTS, IPC, mount, cgroup ns,
   and the pod/host-prepared network ns                             (Ch. 03)
 - a rootfs that is an OverlayFS of nginx's image layers + a writable layer (Ch. 08)
 - resource limits via its cgroup                                   (Ch. 04)
 - a reduced capability set (~14), a seccomp allow-list, no_new_privs (Ch. 05, 06)
When a request hits host :8080, netfilter DNAT sends it to 172.17.0.x:80, into
the container's network namespace, to nginx                          (Ch. 03 §6-7)
```

## The one-paragraph answer

> `docker run nginx` sends an API request to `dockerd`, which pulls the nginx
> image into containerd's content store and unpacks its layers into an OverlayFS
> rootfs, builds an OCI `config.json` from the image config plus your flags, wires
> a network namespace to `docker0` with a veth pair and a DNAT rule for the
> published port, and asks containerd to run it. containerd starts a shim that
> invokes runc, which creates the namespaces, pivots into the overlay rootfs,
> joins the cgroup, drops capabilities, sets `no_new_privs`, installs a seccomp
> filter, and `execve`s nginx. The result is an ordinary Linux process to which
> the kernel has given an isolated, restricted, resource-bounded view of the
> system. Every layer exists to prepare, describe, apply, supervise, or manage
> that one configured process.

## Answering the repository's five questions at each transition

For every arrow in the stack you can now answer the questions from the root
README:

| Transition | Why this layer exists | What it does | Lower mechanism | Observe it | Source |
|---|---|---|---|---|---|
| CLI → dockerd | a stable API for clients | REST → engine actions | HTTP over a Unix socket | `strace`/`docker events` | Ch. 13 §1 |
| dockerd → containerd | separate build/net/volumes from lifecycle | build config, net, volumes; delegate | gRPC | `ctr -n moby` | Ch. 13; Ch. 12 |
| containerd → shim/runc | images/storage/supervision vs one-shot exec | prepare snapshot, run+supervise | shim, OCI bundle | `ps --forest`, `ctr tasks` | Ch. 12 |
| runc → kernel | apply the OCI bundle correctly | clone/mount/pivot/cgroup/caps/seccomp/execve | syscalls | `strace runc` | Ch. 11 |
| kernel | isolate and limit | namespaces, cgroups, overlay, caps, seccomp | task_struct fields | `/proc/<pid>/{ns,cgroup,status,mountinfo}` | Ch. 01–08 |

## Evidence

Lab: [`lab-01-docker-layers`](../../labs/13-moby-docker-engine/lab-01-docker-layers/)
traces a real `docker run` through all these layers on your host.

## Further Reading

- Docker: [docker run reference](https://docs.docker.com/reference/cli/docker/container/run/).
- All prior chapters — this section is their synthesis; each row above points to
  one.
