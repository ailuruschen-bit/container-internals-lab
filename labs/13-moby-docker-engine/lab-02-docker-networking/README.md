# Lab 02 — Docker Networking Is Chapter 03 §6, Automated

## Goal

Confirm that Docker's default bridge network is the veth + bridge + masquerade +
DNAT setup you built by hand in Chapter 03 Lab 06.

## Prerequisites

- Linux VM with Docker, `sudo`, `iproute2`, `iptables` or `nft`.
- Read: Chapter 13 [§2](../../../docs/13-moby-docker-engine/02-networking.md) and
  Chapter 03 [§6](../../../docs/03-namespaces/06-network-namespace.md) / Lab 06.

## Experiment

### Part A — docker0 and veth (compare with Lab 06 Part D)

```bash
ip -brief addr show docker0
docker run -d --name web nginx
ip -brief link | grep veth
bridge link show | grep veth
```

### Part B — The container's network namespace

```bash
PID=$(docker inspect -f '{{.State.Pid}}' web)
sudo nsenter -t $PID -n ip -brief addr        # eth0 172.17.0.x/16
sudo nsenter -t $PID -n ip route              # default via 172.17.0.1 (docker0)
```

**Predict first.** What is the container's default gateway?

### Part C — Masquerade and DNAT (compare with Lab 06 Parts E–F)

```bash
sudo iptables -t nat -S 2>/dev/null | grep -E 'MASQUERADE|DOCKER' | head \
  || sudo nft list ruleset 2>/dev/null | grep -iE 'masquerade|dnat' | head
docker rm -f web
docker run -d -p 8080:80 --name web nginx
sudo iptables -t nat -S 2>/dev/null | grep 8080 \
  || sudo nft list ruleset 2>/dev/null | grep -B2 -A2 8080
curl -s -m 3 localhost:8080 | head -n 1
```

### Part D — host and none networks

```bash
docker run --rm --network none alpine ip -brief addr        # only lo, down
docker run --rm --network host  alpine ip -brief addr | head # the host's interfaces
```

**Predict first.** Which of these creates a new network namespace?

Cleanup: `docker rm -f web 2>/dev/null`.

## Expected observations

**Part A.** `docker0` has an address like `172.17.0.1/16`. After `docker run`, a
`veth*` interface appears, attached to `docker0` in `bridge link` — exactly Lab 06
Part D, created automatically.

**Part B.** The container's `eth0` has a `172.17.0.x/16` address; its default
route is via `172.17.0.1` (docker0), the gateway. This is Lab 06's namespace `a`.

**Part C.** There is a `MASQUERADE` rule for `172.17.0.0/16` (Lab 06 Part E) and,
after `-p 8080:80`, a `DNAT` rule mapping `:8080` to the container's `:80` (Lab 06
Part F). `curl` returns nginx's page.

**Part D.** `--network none` shows only `lo` (down): a new, empty network
namespace (Chapter 03 §6, Lab 06 Part A). `--network host` shows the **host's**
interfaces: no new network namespace (`CLONE_NEWNET` skipped).

## Why this happens

- libnetwork's bridge driver performs the Chapter 03 §6–§7 steps (veth, bridge,
  addressing, masquerade, DNAT) per container; `--network host/none` are "don't
  create / empty" network namespaces (Chapter 13 §2).

## Connection to containers

- Everything you built by hand in Chapter 03 Lab 06 is what Docker does for every
  container. The flags map directly to the mechanisms.

## Questions to think about

1. Map each Docker artifact (`docker0`, `veth*`, MASQUERADE, DNAT) to the exact
   command from Chapter 03 Lab 06 that created its equivalent.
2. Why can `ss` on the host show no listener for a published port, yet `curl`
   works? (Chapter 03 §6-7, Chapter 13 §2.)
3. Two containers on the default bridge — how do they reach each other, and at
   which OSI layer does `docker0` operate? (Chapter 03 §6.)
