# 6. Network Namespace

The network namespace isolates more state than any other namespace. This
section first introduces the minimum networking vocabulary needed, then the
namespace, then how two namespaces are connected to each other and to the
outside world, which is exactly what container networking does.

## 1. The global resource before isolation

### Minimum networking background

A Linux host's **network stack** is the kernel code and state that sends and
receives packets. Its main pieces:

| Piece | What it is | Tool to inspect |
|---|---|---|
| **Network interface** (device) | an endpoint that sends/receives packets: a physical NIC (`eth0`), the loopback device `lo`, or a virtual device | `ip link` |
| **IP address** | an address assigned to an interface, with a prefix length (`10.0.0.2/24`) | `ip addr` |
| **Routing table** | rules deciding which interface (and next-hop gateway) a packet leaves through | `ip route` |
| **Neighbor (ARP) table** | IP-to-MAC mappings on local networks | `ip neigh` |
| **Netfilter rules** | the kernel firewall and NAT, configured by `iptables` or `nftables` | `nft list ruleset`, `iptables -S` |
| **Sockets** | endpoints created by applications; TCP/UDP sockets are bound to **ports** | `ss -tulpn` |
| **Network sysctls** | settings such as IP forwarding | `/proc/sys/net/` |

Without network namespaces, all of these exist once. Consequences:

- only one process can listen on `0.0.0.0:8080`;
- every process sees every interface and can use every route;
- a process with `CAP_NET_ADMIN` can change the firewall or addresses for the
  whole machine;
- abstract Unix domain sockets (names starting with a NUL byte) are visible to
  all processes.

## 2. What the namespace isolates

A network namespace has its own copy of **all of the above**: interfaces,
addresses, routing tables, neighbor tables, netfilter rules, socket port
space, `/proc/net`, most `/proc/sys/net` settings, and abstract Unix sockets.

Rules for devices:

- A newly created network namespace contains **only a loopback device `lo`, and
  it is down**.
- **Every network device belongs to exactly one network namespace.** A device
  can be moved to another namespace (`ip link set DEV netns NS`).
- When a network namespace is destroyed, its virtual devices are destroyed;
  physical devices are moved back to the initial namespace.

What is **not** per network namespace: Unix domain sockets bound to a
**filesystem path** (those are isolated by the mount namespace, like any file),
and the physical hardware itself.

## 3. What changes from the process's perspective

```bash
$ sudo unshare --net bash
# ip addr
1: lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
# ping -c1 127.0.0.1
ping: connect: Network is unreachable
# ip link set lo up; ping -c1 127.0.0.1     → works
# python3 -m http.server 8080                → works even if the host already listens on 8080
```

An empty network namespace is fully isolated and completely useless for
reaching anything else. The interesting part is **connecting** it.

## 4. Kernel API

`clone(CLONE_NEWNET)`, `unshare(CLONE_NEWNET)`, `setns(fd, CLONE_NEWNET)` work
like UTS and IPC: the caller (or child) moves immediately. Creation requires
`CAP_SYS_ADMIN`; configuring interfaces inside requires `CAP_NET_ADMIN` in the
user namespace owning the network namespace.

Sockets remember the namespace in which they were **created**. A process can
create a socket in one namespace, `setns()` into another, and keep using the
first socket. Some tools rely on this.

Interfaces are configured through **netlink** sockets (`AF_NETLINK`), which is
what the `ip` command uses. The `iproute2` suite adds convenience for named
namespaces:

```bash
ip netns add blue            # unshare(CLONE_NEWNET) + bind mount at /run/netns/blue
ip netns exec blue CMD       # setns() + a private mount of /sys for that namespace + execve
ip -n blue addr              # short form of "ip netns exec blue ip addr"
ip netns delete blue         # remove the bind mount (namespace dies if unused)
```

`ip netns exec` also remounts `/sys` so that `/sys/class/net` shows the
namespace's interfaces; sysfs, like procfs, reflects the namespace of whoever
mounted it.

## 5. Connecting namespaces: veth pairs

A **veth pair** is two virtual Ethernet devices connected by a virtual cable:
a packet sent into one end comes out of the other. Put one end in each
namespace, and the namespaces can talk.

```text
 host namespace                               namespace "blue"
┌────────────────────────┐                 ┌────────────────────────┐
│ veth-host 10.200.1.1/24│═════ cable ═════│ veth-blue 10.200.1.2/24│
└────────────────────────┘                 └────────────────────────┘
```

```bash
ip netns add blue
ip link add veth-host type veth peer name veth-blue
ip link set veth-blue netns blue
ip addr add 10.200.1.1/24 dev veth-host && ip link set veth-host up
ip -n blue addr add 10.200.1.2/24 dev veth-blue
ip -n blue link set veth-blue up && ip -n blue link set lo up
ping -c1 10.200.1.2
```

## 6. Many namespaces: a bridge

With many containers, giving each a veth to the host and a separate subnet is
unwieldy. A **Linux bridge** is a virtual Layer-2 switch inside a namespace.
Attach the host end of every veth pair to the bridge, give the bridge an IP
address, and all namespaces share one subnet with the bridge as their gateway:

```text
                       host namespace
 ┌──────────────────────────────────────────────────────────┐
 │        br0 10.200.0.1/24  (bridge = virtual switch)      │
 │         │                     │                          │
 │     veth-a                veth-b                         │
 └─────────┼─────────────────────┼──────────────────────────┘
           │ cable               │ cable
 ┌─────────┼─────────┐   ┌───────┼───────────┐
 │ eth0 10.200.0.2   │   │ eth0 10.200.0.3   │
 │ default via .0.1  │   │ default via .0.1  │
 │ namespace "a"     │   │ namespace "b"     │
 └───────────────────┘   └───────────────────┘
```

This is exactly Docker's default `bridge` network: the bridge is called
`docker0` (typically `172.17.0.1/16`), and each container's `eth0` is one end of
a veth pair whose other end is attached to `docker0`.

## 7. Reaching the outside world: routing and NAT

Namespace `a` can now reach `10.200.0.1` and namespace `b`. To reach the
Internet, two more things are needed on the host:

1. **IP forwarding**: `sysctl -w net.ipv4.ip_forward=1`, so the host routes
   packets between `br0` and its external interface.
2. **Source NAT (masquerade)**: packets from `10.200.0.0/24` must leave with the
   host's external IP address, because the outside network has no route back to
   the private subnet:

   ```bash
   nft add table ip labnat
   nft add chain ip labnat postrouting '{ type nat hook postrouting priority 100 ; }'
   nft add rule ip labnat postrouting ip saddr 10.200.0.0/24 oifname != "br0" masquerade
   ```

   (Equivalent classic form:
   `iptables -t nat -A POSTROUTING -s 10.200.0.0/24 ! -o br0 -j MASQUERADE`.)

**Publishing a port** (`docker run -p 8080:80`) is the reverse direction:
**destination NAT** on the host rewrites packets arriving at host port 8080 to
`<container IP>:80`. Docker implements this with netfilter DNAT rules (and,
in some cases, a user-space proxy process, `docker-proxy`).

Note that these firewall rules live in the **host's** network namespace. They
are applied to the container's traffic because that traffic is routed through
the host namespace.

## 8. How container runtimes use it

- Low-level runtimes such as runc only **create** (or join) the network
  namespace, according to the OCI configuration. They do not create interfaces.
- The interfaces, addresses, routes, bridge, and NAT rules are created by a
  higher layer: Docker's network driver (libnetwork), or a **CNI plugin**
  invoked by containerd's CRI implementation in Kubernetes. A CNI plugin is
  given the path of a network namespace file (for example
  `/var/run/netns/cni-1234...`) and performs the same steps as sections 5–7.
- Kubernetes gives each **pod** one network namespace, created with the pod's
  sandbox ("pause") container and pinned by a bind mount. All containers in the
  pod join it with `setns()`, so they share `localhost` and the port space.
- `--network host` / `hostNetwork: true` skip the new namespace. The container
  then shares the host's interfaces and ports, and its processes can bind any
  host port.
- `--network none` creates a namespace with only `lo`, exactly like section 3.
- JVM applications inside a container see only `lo` and `eth0`; a JVM binding
  `0.0.0.0:8080` binds inside the namespace, and nothing on the host is
  listening on 8080 unless the port is published with DNAT.

## Evidence

Lab: [`lab-06-network-namespace`](../../labs/03-namespaces/lab-06-network-namespace/)

## Further Reading

- [`network_namespaces(7)`](https://man7.org/linux/man-pages/man7/network_namespaces.7.html)
  — the definitive list of what is isolated, and the device lifetime rules.
- [`veth(4)`](https://man7.org/linux/man-pages/man4/veth.4.html) — one page;
  explains the pair semantics and how to move one end.
- [`ip-netns(8)`](https://man7.org/linux/man-pages/man8/ip-netns.8.html) — named
  namespaces, `/run/netns`, and why `ip netns exec` remounts `/sys`.
- [`ip-link(8)`](https://man7.org/linux/man-pages/man8/ip-link.8.html) — the
  `veth`, `bridge`, and `netns` sub-commands used in the lab.
- CNI specification: [containernetworking/cni SPEC.md](https://github.com/containernetworking/cni/blob/main/SPEC.md)
  — short and readable; shows how a network plugin receives a namespace path
  and configures it. Also read the reference
  [bridge plugin](https://www.cni.dev/plugins/current/main/bridge/) docs, which
  describe the setup built manually in this section.
- Docker documentation: [Bridge network driver](https://docs.docker.com/engine/network/drivers/bridge/)
  and [Packet filtering and firewalls](https://docs.docker.com/engine/network/packet-filtering-firewalls/)
  — how `docker0`, masquerade, and port publishing map to the mechanisms above.
- nftables wiki: [Performing Network Address Translation (NAT)](https://wiki.nftables.org/wiki-nftables/index.php/Performing_Network_Address_Translation_(NAT))
  — the `masquerade` and `dnat` rules used above, in their authoritative form.
