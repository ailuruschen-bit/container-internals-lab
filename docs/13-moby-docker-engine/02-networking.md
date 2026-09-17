# 2. Networking: libnetwork

Chapter 03 §6 and Lab 06 built container networking by hand: a network namespace,
a veth pair, a bridge, masquerade, and DNAT for port publishing. Docker's
**libnetwork** automates exactly that. This section maps Docker's networking to
the mechanisms you already built, so nothing here is new.

## The Container Network Model (CNM)

libnetwork organizes networking around three objects:

| CNM object | Is | Chapter 03 §6 equivalent |
|---|---|---|
| **Network** | a set of endpoints that can communicate; has a driver | the bridge `docker0` and its subnet |
| **Endpoint** | a connection point for one container | one end of a veth pair |
| **Sandbox** | a container's isolated network stack | the network namespace |

## The default bridge driver

`docker run nginx` with no network flags attaches the container to the default
**bridge** network. libnetwork does, per container, what Chapter 03 Lab 06 did by
hand:

```text
1. create a network namespace (sandbox) for the container      Ch. 03 §6
2. create a veth pair; put one end in the namespace as eth0,
   attach the other end to the bridge docker0                   Ch. 03 §6
3. assign the container an IP from docker0's subnet (e.g. 172.17.0.0/16)
   and a default route via docker0 (172.17.0.1)                 Ch. 03 §6
4. ensure IP forwarding + a masquerade rule so the container reaches
   the outside with the host's IP                               Ch. 03 §6, §7
```

On a Docker host, `ip link` shows `docker0` and `veth*` interfaces, and
`bridge link` shows the veths attached to `docker0` — precisely the Chapter 03
Lab 06 Part D state, created for you.

## Publishing a port

`docker run -p 8080:80 nginx` is Chapter 03 §6/§7's destination NAT:

```text
libnetwork adds a netfilter DNAT rule (the DOCKER chain):
   host :8080  →  <container IP>:80                              Ch. 03 §6-7
plus, in some configurations, a small userspace docker-proxy process
   that also listens on :8080 (a fallback / hairpin helper)
```

This is why, as Chapter 03 Lab 06 Part F showed, `ss` on the host may show a
docker-proxy listener (or nothing) for a published port even though traffic
reaches the container: the delivery is done by DNAT, not by a host process
bound to the port for the container.

## Other network drivers

| Driver | What it does | Mechanism |
|---|---|---|
| `bridge` (default) | the above | Ch. 03 §6 |
| `host` | no network namespace; share the host's | Ch. 03 §6 (skip `CLONE_NEWNET`) |
| `none` | a namespace with only `lo` | Ch. 03 §6, §3 |
| `overlay` | multi-host networking (Swarm) via VXLAN | beyond this repo; Chapter 03 §6 "Questions" hinted at it |
| `macvlan`/`ipvlan` | give the container a MAC/IP on the physical LAN | kernel macvlan/ipvlan |

`--network host` and `--network none` are literally "do or don't create a network
namespace", which you can now predict exactly.

## DNS and /etc/hosts

Docker runs an embedded DNS resolver for user-defined networks (so containers can
reach each other by name) and bind-mounts a generated `/etc/resolv.conf`,
`/etc/hosts`, and `/etc/hostname` into the container (Chapter 02 §3 single-file
bind mounts; Chapter 03 §2 for the hostname). This is why editing those files
inside a container can behave oddly (Chapter 02 §3, the single-file bind-mount
inode-pinning caveat).

## Why this matters

- It closes the loop from Chapter 03 §6: everything you built by hand
  (veth/bridge/masquerade/DNAT) is what Docker does automatically, and the flags
  (`-p`, `--network host/none`) map to those exact operations.
- It explains observable Docker facts: `docker0`, `veth*` interfaces, the DOCKER
  iptables chains, docker-proxy, and the bind-mounted `/etc/hosts`.

## Evidence

Lab: [`lab-02-docker-networking`](../../labs/13-moby-docker-engine/lab-02-docker-networking/)

## Further Reading

- Docker: [Bridge network driver](https://docs.docker.com/engine/network/drivers/bridge/),
  [Networking overview](https://docs.docker.com/engine/network/),
  [Packet filtering and firewalls](https://docs.docker.com/engine/network/packet-filtering-firewalls/).
- [moby/libnetwork](https://github.com/moby/libnetwork) (now vendored into
  moby/moby) and the [CNM design](https://github.com/moby/libnetwork/blob/master/docs/design.md).
- Chapter 03 §6 and Lab 06 — the same networking, built by hand.
