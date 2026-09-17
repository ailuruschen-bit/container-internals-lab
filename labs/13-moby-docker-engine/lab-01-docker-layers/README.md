# Lab 01 — Trace docker run Through Every Layer

## Goal

On a real Docker host, observe one `docker run` passing through all the layers of
Chapters 11–13, and confirm the nginx process is an ordinary Linux process with
the isolation of Chapters 01–08.

## Prerequisites

- Linux VM with Docker, `sudo`, `jq`, `ctr`, `procps`.
- Read: Chapter 13 [§1](../../../docs/13-moby-docker-engine/01-architecture.md)
  and [§4](../../../docs/13-moby-docker-engine/04-docker-run-nginx.md).

## Experiment

### Part A — Run it

```bash
docker run -d -p 8080:80 --name web nginx
docker ps --filter name=web
```

### Part B — dockerd → containerd (the moby namespace)

```bash
sudo ctr -n moby containers ls | grep "$(docker inspect -f '{{.Id}}' web | cut -c1-12)" || sudo ctr -n moby containers ls | head
sudo ctr -n moby tasks ls | head
```

**Predict first.** Will Docker's container appear in containerd's `moby`
namespace?

### Part C — The shim is the parent, runc has exited

```bash
PID=$(docker inspect -f '{{.State.Pid}}' web)
echo "nginx master pid on host: $PID"
ps -o pid,ppid,comm -p "$PID"
PPID=$(ps -o ppid= -p "$PID" | tr -d ' '); ps -o pid,comm -p "$PPID"
pgrep -a runc || echo "no runc process lingering"
ps -ef --forest | grep -E 'containerd|shim|nginx' | grep -v grep | head
```

### Part D — The kernel gave it the Chapter 01–08 properties

```bash
sudo ls -l /proc/$PID/ns                       # its namespaces (Ch. 03)
cat /proc/$PID/cgroup                            # its cgroup (Ch. 04)
sudo cat /proc/$PID/status | grep -E 'CapBnd|NoNewPrivs|Seccomp'   # Ch. 05, 06
sudo grep ' / ' /proc/$PID/mountinfo | grep overlay                # Ch. 08
sudo readlink /proc/$PID/root                    # its rootfs (Ch. 07)
sudo nsenter -t $PID -n ip addr                  # its network ns: eth0 172.17.x (Ch. 03 §6)
```

### Part E — The published port is DNAT, not a host listener

```bash
sudo iptables -t nat -S 2>/dev/null | grep -E '8080|DOCKER' | head || sudo nft list ruleset 2>/dev/null | grep -A2 8080 | head
curl -s -m 3 http://localhost:8080/ | head -n 5
ss -ltnp 2>/dev/null | grep 8080 || echo "(delivery is via DNAT; may be docker-proxy or none)"
```

### Part F — Stop it: the signal sequence

```bash
time docker stop web            # SIGTERM, grace period, then SIGKILL (Ch. 01 §6)
docker inspect -f '{{.State.ExitCode}}' web
docker rm web
```

## Expected observations

**Part A.** `web` is running.

**Part B.** The container and task appear under containerd's `moby` namespace:
`dockerd` created them via containerd (Chapter 13 §1).

**Part C.** The nginx master's parent is a `containerd-shim-runc-v2`, not
`containerd` or `dockerd`. No `runc` is running (Chapter 12 §4). `ps --forest`
shows the shim under the system manager with nginx beneath it.

**Part D.** `/proc/$PID/ns` shows namespace inodes distinct from the host's;
`/proc/$PID/cgroup` shows a Docker cgroup path; `status` shows a reduced `CapBnd`,
`NoNewPrivs` (0 by default for Docker unless configured), `Seccomp: 2`; the rootfs
mount is `overlay`; `readlink root` points into the overlay merged dir; `nsenter`
shows `eth0` with a `172.17.x` address.

**Part E.** A DNAT rule maps `:8080` to the container IP:80. `curl` returns the
nginx welcome page. `ss` may show `docker-proxy` on 8080 or nothing bound for the
container — delivery is by NAT (Chapter 03 §6-7, Chapter 13 §2).

**Part F.** `docker stop` sends `SIGTERM`; nginx handles it and exits promptly, so
`stop` is fast and the exit code is 0 (nginx's entrypoint traps it). A process
that ignored SIGTERM would take ~10s and exit 137 (Chapter 01 §6).

## Why this happens

- Every layer of Chapter 13 §4 is present and observable: dockerd → containerd
  (moby ns) → shim → (runc, now exited) → nginx, with the kernel providing the
  Chapters 01–08 properties.

## Connection to containers

- This lab **is** the repository's thesis, made concrete on your machine: a
  single `docker run` is the whole stack you have studied, and the nginx process
  is an ordinary Linux process with a configured, isolated view.

## Questions to think about

1. For each item in Part D, name the chapter that explains it.
2. Why is the nginx master PID on the host different from its PID inside the
   container? (Chapter 03 §3; `sudo cat /proc/$PID/status | grep NSpid`.)
3. Using Chapter 05 §5, what would make this root-in-container dangerous, and
   which single `docker run` flag would mitigate it?
