# Lab 06 — Network Namespace: From Empty Stack to a Hand-Built docker0

## Goal

Produce evidence that:

1. a new network namespace has only a loopback device, which is down;
2. port numbers and listening sockets are per namespace;
3. a veth pair connects two namespaces;
4. a bridge connects many namespaces on one subnet;
5. IP forwarding plus masquerade lets a namespace reach external networks;
6. destination NAT on the host "publishes" a port of a namespace;
7. processes that join the same network namespace share `localhost`.

## Prerequisites

- Linux VM with `sudo`, `iproute2`, `nftables` (`nft`), `python3`, `curl`,
  `iputils-ping`.
- Read: [6. Network namespace](../../../docs/03-namespaces/06-network-namespace.md).
- Use a VM **without Docker installed**, or be aware that Docker sets the
  iptables `FORWARD` policy to `DROP`, which blocks Part E. (Workaround on a
  Docker host: `sudo iptables -I DOCKER-USER -s 10.200.0.0/24 -j ACCEPT` and
  `sudo iptables -I DOCKER-USER -d 10.200.0.0/24 -j ACCEPT`, removed afterwards
  with `-D`.)

If anything goes wrong, `sudo ./teardown.sh` removes all lab state.

## Experiment

### Part A — An empty network stack

```bash
sudo ip netns add blue
sudo ip netns exec blue ip addr
sudo ip netns exec blue ping -c1 -W1 127.0.0.1; echo "exit code: $?"
sudo ip netns exec blue ip link set lo up
sudo ip netns exec blue ping -c1 -W1 127.0.0.1
sudo ip netns exec blue ip route
sudo ip netns exec blue ls /sys/class/net
ls /sys/class/net
```

### Part B — Ports are per namespace

```bash
python3 -m http.server 8080 --bind 127.0.0.1 >/dev/null 2>&1 &
HOSTSRV=$!
sudo ip netns exec blue python3 -m http.server 8080 --bind 127.0.0.1 >/dev/null 2>&1 &
sleep 1
ss -ltn 'sport = :8080'
sudo ip netns exec blue ss -ltn 'sport = :8080'
kill $HOSTSRV; sudo pkill -f 'http.server 8080'
```

**Predict first.** Will the second server fail with "Address already in use"?

### Part C — A veth pair

```bash
sudo ip link add veth-host type veth peer name veth-blue
ip -brief link | grep veth
sudo ip link set veth-blue netns blue
ip -brief link | grep veth
sudo ip addr add 10.200.1.1/24 dev veth-host
sudo ip link set veth-host up
sudo ip -n blue addr add 10.200.1.2/24 dev veth-blue
sudo ip -n blue link set veth-blue up
ping -c2 10.200.1.2
sudo ip netns exec blue ping -c2 10.200.1.1
sudo ip netns exec blue ping -c1 -W1 8.8.8.8; echo "exit code: $?"
```

**Predict first.** After moving `veth-blue`, which `ip link` output still lists
it? Can `blue` reach `8.8.8.8`?

Delete the namespace and observe the host end:

```bash
sudo ip netns delete blue
ip -brief link | grep veth || echo "veth-host disappeared too"
```

### Part D — A bridge for two namespaces

```bash
chmod +x setup-bridge.sh teardown.sh
sudo ./setup-bridge.sh
bridge link show
sudo ip netns exec a ip -brief addr
sudo ip netns exec a ping -c2 10.200.0.3          # a → b through br0
sudo ip netns exec b ip neigh
```

### Part E — Reaching the outside world

```bash
sudo ip netns exec a ping -c2 -W2 8.8.8.8
sudo nft list table ip labnat
```

In a second terminal, watch the source address on the host's external
interface while pinging again (replace `eth0` with your interface from
`ip route show default`):

```bash
sudo tcpdump -ni eth0 icmp and host 8.8.8.8
```

Then disable masquerade and test again:

```bash
sudo nft flush chain ip labnat postrouting
sudo ip netns exec a ping -c2 -W2 8.8.8.8; echo "exit code: $?"
sudo nft add rule ip labnat postrouting ip saddr 10.200.0.0/24 oifname != "br0" masquerade
```

### Part F — Publishing a port with DNAT

Start a server in namespace `a`:

```bash
sudo ip netns exec a python3 -m http.server 80 >/dev/null 2>&1 &
HOSTIP=$(ip -4 route get 8.8.8.8 | awk '{for (i=1;i<=NF;i++) if ($i=="src") print $(i+1)}')
echo "host IP: $HOSTIP"
curl -s -m 2 http://$HOSTIP:8080/ >/dev/null; echo "before DNAT exit code: $?"
```

Add destination NAT for traffic arriving from other machines (`prerouting`) and
for traffic generated on the host itself (`output`):

```bash
sudo nft add chain ip labnat prerouting '{ type nat hook prerouting priority -100 ; }'
sudo nft add chain ip labnat output '{ type nat hook output priority -100 ; }'
sudo nft add rule ip labnat prerouting ip daddr $HOSTIP tcp dport 8080 dnat to 10.200.0.2:80
sudo nft add rule ip labnat output     ip daddr $HOSTIP tcp dport 8080 dnat to 10.200.0.2:80
curl -s -m 2 http://$HOSTIP:8080/ | head -n 3
ss -ltn 'sport = :8080' | tail -n +2 | wc -l
sudo pkill -f 'http.server 80'
```

### Part G — Sharing one network namespace ("pod")

```bash
sudo ip netns exec b python3 -m http.server 9000 --bind 127.0.0.1 >/dev/null 2>&1 &
sleep 1
sudo ip netns exec b curl -s -m 2 http://127.0.0.1:9000/ | head -n 1
sudo ip netns exec a curl -s -m 2 http://127.0.0.1:9000/; echo "from a, exit code: $?"
curl -s -m 2 http://127.0.0.1:9000/; echo "from host, exit code: $?"
sudo pkill -f 'http.server 9000'
```

### Cleanup

```bash
sudo ./teardown.sh
```

## Expected observations

**Part A.** Only `lo`, state `DOWN`. `ping` fails with `connect: Network is
unreachable`; after `ip link set lo up`, it succeeds. The routing table is
empty except local routes after `lo` is up (`ip route` may print nothing).
`/sys/class/net` inside lists only `lo`; the host lists its real interfaces.

**Part B.** Both servers start. `ss` on the host shows one listener; `ss` in
`blue` shows another. No "Address already in use".

**Part C.** Before the move, both ends are listed on the host (as
`veth-host@veth-blue` and `veth-blue@veth-host`); after it, only `veth-host`
(its peer shown as `@if<N>`). Pings between `10.200.1.1` and `10.200.1.2`
succeed. `blue` cannot reach `8.8.8.8` (`Network is unreachable`: no default
route). After deleting the namespace, `veth-host` is gone as well.

**Part D.** `bridge link show` lists `veth-a` and `veth-b` with `master br0`.
`a` pings `b`. `ip neigh` in `b` shows `10.200.0.2` with a MAC address.

**Part E.** With masquerade, the ping succeeds, and `tcpdump` shows packets
with the **host's** IP address as the source, not `10.200.0.2`. After flushing
the chain, the ping times out (100% packet loss): packets leave with source
`10.200.0.2`, and replies never come back.

**Part F.** Before DNAT, `curl` fails (exit code 7, connection refused). After
DNAT, it returns the start of an HTML directory listing, although `ss` shows
**no process listening on port 8080** in the host namespace (`0`).

**Part G.** Inside `b`, `curl` to `127.0.0.1:9000` works. From `a` and from the
host it fails (exit code 7): each namespace has its own loopback.

## Why this happens

- **A.** `copy_net_ns()` creates a new `struct net` with a loopback device
  registered but not up. `ip netns exec` mounts a new sysfs so `/sys/class/net`
  reflects the namespace.
- **B, G.** Sockets and the port table belong to a `struct net`.
- **C.** A veth pair is one driver instance with two devices; moving a device
  changes its `struct net`. Destroying the namespace unregisters the device,
  which removes its peer.
- **D.** The bridge forwards Ethernet frames between attached ports by MAC
  address, as a switch does.
- **E.** Without SNAT, the external network has no route to `10.200.0.0/24`.
  Masquerade rewrites the source to the outgoing interface's address and tracks
  the connection (conntrack) to reverse the translation on replies.
- **F.** The DNAT rule rewrites the destination before routing; the packet is
  then routed to `br0` and delivered to namespace `a`. No socket is bound to
  port 8080 on the host.

## Connection to containers

- Part A is `docker run --network none`.
- Part D + E is Docker's default bridge network (`docker0`, veth pairs,
  masquerade). Compare with `ip link`, `bridge link`, and `nft list ruleset` on
  a Docker host.
- Part F is `docker run -p 8080:80`. It explains why `ss` on the host may not
  show a listener for a published port.
- Part G is a Kubernetes pod: containers joining one network namespace share
  `localhost`; other pods do not.

## Questions to think about

1. In Part F, where would you look to find why a published port is not reachable
   from another machine, but works from the host itself?
2. How would two namespaces on **different hosts** communicate? What would need
   to be added to this lab? (This is the problem solved by overlay network CNI
   plugins.)
3. A JVM in namespace `a` calls `InetAddress.getLocalHost().getHostAddress()`.
   Which namespaces and files influence the answer?
4. Why is it safe to let a container have `CAP_NET_ADMIN` inside its own
   network namespace, but dangerous with `--network host`?
