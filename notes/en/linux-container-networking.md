# How Linux Container Networking Works: From Network Namespaces to NAT

[中文](../linux-container-networking.md) | **English** | [日本語](../ja/linux-container-networking.md)

Container networking looks like a small, self-contained network: a container has its own network interface, IP address, routes, and ports, and it can communicate with other containers and with the internet.

Docker does not reimplement the TCP/IP stack. It mostly composes mechanisms the Linux kernel already provides: network namespaces, veth pairs, bridges, routing, and Netfilter.

## 1. Network Namespaces: An Independent Network Environment

How can processes managed by the same Linux kernel have different network environments?

A network namespace (netns) isolates a set of network-related kernel resources, including:

- network interfaces and IP addresses;
- the IPv4 and IPv6 protocol stacks;
- routing tables;
- firewall rules;
- the port space used by sockets;
- network views such as `/proc/net` and `/sys/class/net`.

As a result, two processes can each listen on `0.0.0.0:80` in their own netns without conflicting over the same port.

A netns can be likened to the independent networking context of a computer. However, creating a new netns only yields an isolated network environment; it does not automatically come with external connectivity. Initially it usually contains nothing but the loopback interface `lo`, and network interfaces, IP addresses, and routes still need to be configured.

## 2. veth Pairs: Virtual Cables Between Network Environments

If the network resources of different network namespaces are isolated from each other, how do they connect?

A veth pair is a pair of connected virtual Ethernet interfaces. Whatever is sent from one end is delivered by the kernel to the other end:

```text
NetNS A                         NetNS B
  veth-a  ◄──────────────────►  veth-b
```

The two ends can be placed in different network namespaces. The transfer is handled entirely by the Linux kernel and never touches a physical NIC.

Calling it a "virtual cable" is intuitive, but a more complete picture is that a veth pair includes the virtual network interfaces at both ends of the cable. Each end has its own interface name, MAC address, and statistics.

Docker typically moves one end into the container's netns, where it is renamed `eth0`, and leaves the other end in the host's netns, attached to a Linux bridge.

## 3. Linux Bridges: A Virtual Switch for Multiple Containers

A single veth pair can connect only two endpoints. If several containers need to join the same network, a device that can connect many interfaces is required.

A Linux bridge is a virtual Layer 2 switch implemented in the kernel. Multiple host-side veth interfaces can be attached to the same bridge:

```text
Container A                     Container B
  eth0                            eth0
    │                              │
  veth                            veth
    └──────── Linux Bridge ─────────┘
```

The bridge learns the source MAC addresses of incoming frames and uses the destination MAC address to decide which port to forward a frame out of. When the destination is unknown or the frame is a broadcast, it floods the frame to the other appropriate ports.

If two containers are on the same bridge network and their IP addresses and subnets are configured correctly, they can usually communicate directly through the bridge without any NAT:

```text
Container A
    → veth
    → Linux Bridge
    → veth
    → Container B
```

A bridge itself operates at Layer 2, but the host usually also assigns an IP address to the bridge interface. That address can serve as the gateway in the container's default route, letting traffic leave the container network and enter the host's Layer 3 routing system.

## 4. Routing and NAT: How Containers Reach External Networks

The internal IP addresses used by containers are usually not directly routable from external networks. In the default `nat` mode commonly used by Docker bridge networks, the host takes on the role of gateway and address translator.

When a container sends a packet to the internet, the path looks roughly like this:

```text
Container eth0
    → veth pair
    → Linux Bridge
    → Host IP routing
    → Netfilter MASQUERADE / SNAT
    → Host external NIC
    → Internet
```

Two capabilities are involved here:

1. **Forwarding**: the host forwards packets from the container bridge to an external interface.
2. **Source address translation**: the container's internal source address is rewritten to an address the host uses externally, with the source port also rewritten when necessary.

When the host's external address may change, MASQUERADE is typically used; it can be understood as a form of SNAT that automatically picks the address of the outgoing interface.

Netfilter's connection tracking system, conntrack, records each connection and its translation state. When the external response comes back, the kernel uses this state to restore the original destination and routes the packet back to the correct container. This is how multiple containers can share the host's external IP address.

NAT is not a prerequisite for container networking. If the external network already knows how to route to the container subnet, direct routing can be used instead. Docker bridge networks also support other gateway modes such as `routed`; what is discussed here is the most common, default NAT model.

## 5. Port Mapping: Letting External Clients Reach Container Services

Outbound NAT mainly addresses connections that a container initiates. But if an external client knows only the host's address, how can it initiate a connection to a service inside a container?

Docker's port publishing sets up, in advance, a forwarding relationship between a host address and a container address:

```bash
docker run -p 8080:80 nginx
```

This publishes port `8080` on the host to port `80` in the container. In the default NAT mode on Linux, the typical inbound path is:

```text
Client
    → Host IP:8080
    → Netfilter DNAT
    → Container IP:80
    → Linux Bridge
    → veth pair
    → Container service
```

DNAT rewrites the destination address and port of inbound packets, forwarding rules allow the packets through, and conntrack ensures that responses return along the correct reverse translation path. Depending on where the traffic comes from and how Docker is configured, the actual implementation may also involve additional Netfilter rules or a userland proxy.

Port mapping and conntrack have different responsibilities:

- Port mapping defines in advance which host port is forwarded to which container service.
- Conntrack dynamically records every actual connection and its translation state.

If `-p` does not specify a host IP, the port is usually bound on all host addresses. Whether it is actually reachable from outside also depends on the host firewall, routing, and cloud-platform security rules.

## 6. How Docker Puts These Mechanisms Together

A typical Linux bridge network can be drawn like this:

```text
                         Internet
                            │
                Host external network interface
                            │
                  IP Routing / Netfilter NAT
                            │
             Linux Bridge (e.g., docker0)
                     /               \
               host veth          host veth
                   │                  │
               veth Pair          veth Pair
                   │                  │
          Container A NetNS   Container B NetNS
             eth0                 eth0
          172.17.0.2           172.17.0.3
```

The three typical communication scenarios can be summarized as follows:

| Scenario | Main mechanisms |
|---|---|
| Container A → Container B | veth pair + Linux bridge |
| Container A → Internet | veth pair + bridge + routing + MASQUERADE/SNAT |
| Internet → service in Container A | Port mapping + DNAT + routing + bridge + veth pair |

Each mechanism answers one question:

```text
How does an isolated netns connect to the outside?   → veth pair
How do multiple containers join the same network?    → Linux bridge
How do internal addresses reach, or get reached by,
the outside world?                                   → Routing, NAT, and port publishing
```

With Docker Desktop on macOS, containers and all of these Linux networking mechanisms run inside a hidden Linux virtual machine. Port access from macOS to the VM and on to a container also passes through a forwarding layer provided by Docker Desktop, so you cannot treat macOS as a native Linux Docker host and inspect every interface and Netfilter rule directly.

## 7. Relating This to AWS VPC Networking

Linux container networking and AWS VPCs sit at different levels of implementation, but they can be related through the problems they solve:

| AWS component | Main role |
|---|---|
| NAT gateway | Lets resources in private subnets initiate connections to external IPv4 networks and receive the corresponding responses |
| Internet gateway | Serves as the internet target in VPC route tables; enables public communication when addressing and routing requirements are met |
| ALB / NLB | Accepts inbound connections through listeners and distributes traffic to backend services in target groups |
| Interface endpoint | Provides access to supported services through PrivateLink, using private IP addresses in your subnets |
| Gateway endpoint | Provides private access to S3 and DynamoDB through route tables |

### NAT Gateways vs. Docker's Outbound NAT

Both address the same problem, letting internal private addresses initiate connections to external networks and allowing the response traffic back in. However, an AWS NAT gateway is a managed network service run by the cloud platform, not a set of Netfilter rules that Docker configures on a single Linux host.

A NAT gateway does not provide arbitrary inbound port forwarding from the internet. To expose a service publicly, AWS typically relies on resources with public addresses or on entry points such as an ALB or NLB.

### An Internet Gateway Is Not a Switch That Works on Its Own

Attaching an internet gateway to a VPC does not mean every instance can immediately reach the internet. The route tables must be configured correctly, and for IPv4 an instance usually also needs a public IPv4 address or an Elastic IP address. Security groups and network ACLs must also allow the relevant traffic.

### VPC Endpoints vs. Port Mapping

An interface endpoint is not a public entry point for a service. It uses elastic network interfaces with private IP addresses in your subnets to let clients in the VPC privately access AWS services or PrivateLink-powered services.

A gateway endpoint, by contrast, uses route tables to steer traffic destined for S3 or DynamoDB to the endpoint. Both kinds of VPC endpoints are about avoiding a public entry point or NAT; neither is the same mechanism as port mapping, which forwards a host port to a container port.

## My Current Understanding

Linux container networking is infrastructure assembled from virtual devices and networking features in the kernel:

```text
Network Namespace  ≈ an independent networking context
veth Pair          ≈ a virtual cable plus the interfaces at both ends
Linux Bridge       ≈ a Layer 2 switch
Routing + NAT      ≈ a gateway's routing and address translation
Port Mapping       ≈ preconfigured inbound port forwarding
```

A container can have its own IP address, routes, and port space without monopolizing a physical NIC and without a separate Linux kernel. Docker's role is to automatically create, connect, configure, and clean up these Linux mechanisms in step with the container lifecycle.
