# Linux 容器网络底层原理：从 Network Namespace 到 NAT

容器网络看起来像一套独立的小型网络：容器有自己的网卡、IP、路由和端口，还能与其他容器及互联网通信。

Docker 并没有重新实现 TCP/IP 协议栈。它主要是在组合 Linux 内核已有的 Network Namespace、veth、Bridge、路由和 Netfilter 等机制。

## 1. Network Namespace：独立的网络环境

同一个 Linux 内核管理的进程，为什么可以拥有不同的网络环境？

Network Namespace（NetNS）隔离了与网络有关的一组内核资源，包括：

- 网络接口与 IP 地址；
- IPv4、IPv6 协议栈；
- 路由表；
- 防火墙规则；
- Socket 使用的端口空间；
- `/proc/net`、`/sys/class/net` 等网络视图。

因此，两个进程可以分别在自己的 NetNS 中监听 `0.0.0.0:80`，不会因为端口相同而冲突。

NetNS 可以类比成一台计算机的独立网络上下文。不过，新建 NetNS 只会得到一套隔离的网络环境，不会自动获得外部连接。它最初通常只有回环接口 `lo`，还需要配置网络接口、IP 和路由。

## 2. veth Pair：连接不同网络环境的虚拟网线

不同 NetNS 的网络资源已经相互隔离，它们怎样建立连接？

veth Pair 是一对相连的虚拟以太网接口。数据从一端发送后，会由内核交给另一端接收：

```text
NetNS A                         NetNS B
  veth-a  ◄──────────────────►  veth-b
```

两端可以被分别放进不同的 NetNS。数据传输完全由 Linux 内核完成，不需要经过真实物理网卡。

把它类比成“虚拟网线”很直观，但更完整的理解是：veth Pair 同时包含网线两端的虚拟网络接口。每一端都有自己的接口名称、MAC 地址和统计信息。

Docker 通常把一端放进容器的 NetNS，并在容器中将它命名为 `eth0`；另一端留在宿主机的 NetNS，接入 Linux Bridge。

## 3. Linux Bridge：连接多个容器的虚拟交换机

一对 veth 只能连接两个端点。如果多个容器都要接入同一网络，就需要一个能够连接多个接口的设备。

Linux Bridge 是内核实现的虚拟二层交换机。宿主机一侧的多个 veth 接口可以接入同一个 Bridge：

```text
Container A                     Container B
  eth0                            eth0
    │                              │
  veth                            veth
    └──────── Linux Bridge ─────────┘
```

Bridge 会学习帧的源 MAC 地址，并根据目标 MAC 地址决定从哪个端口转发。目标未知或属于广播时，它会向其他适当端口转发。

如果两个容器位于同一个 Bridge 网络、IP 和子网配置正确，它们通常可以直接通过 Bridge 通信，不需要经过 NAT：

```text
Container A
    → veth
    → Linux Bridge
    → veth
    → Container B
```

Bridge 本身工作在二层，但宿主机通常还会为 Bridge 接口配置一个 IP。这个 IP 可以成为容器默认路由中的网关地址，使数据从容器网络进入宿主机的三层路由系统。

## 4. 路由与 NAT：容器如何访问外部网络

容器使用的内部 IP 通常不会被外部网络直接路由。以 Docker Bridge 网络常见的默认 `nat` 模式为例，宿主机会承担网关和地址转换的角色。

容器向互联网发包时，路径大致如下：

```text
容器 eth0
    → veth Pair
    → Linux Bridge
    → 宿主机 IP 路由
    → Netfilter MASQUERADE / SNAT
    → 宿主机外部网卡
    → Internet
```

这里需要两类能力：

1. **路由转发**：宿主机把数据包从容器 Bridge 转发到外部接口。
2. **源地址转换**：将容器的内部源地址改写成宿主机对外使用的地址，必要时同时改写源端口。

当宿主机外部地址可能变化时，通常使用 MASQUERADE；它可以理解为自动选择出口接口地址的一种 SNAT。

Netfilter 的连接跟踪系统 Conntrack 会记录连接及地址转换状态。外部响应返回后，内核根据这份状态恢复原来的目标信息，再将数据包路由回正确的容器。多个容器因此可以共享宿主机的对外 IP。

NAT 不是容器联网的必备条件。如果外部网络已经知道怎样路由到容器网段，也可以使用直接路由。Docker Bridge 网络也支持 `routed` 等网关模式；这里讨论的是最常见的默认 NAT 模型。

## 5. Port Mapping：允许外部访问容器服务

出站 NAT 主要解决容器主动建立连接的问题。如果外部客户端只知道宿主机地址，怎样主动访问容器中的服务？

Docker 的端口发布可以预先建立宿主机地址与容器地址之间的转发关系：

```bash
docker run -p 8080:80 nginx
```

它表示将宿主机的 `8080` 端口发布到容器的 `80` 端口。在 Linux 上的默认 NAT 模式中，典型入站路径是：

```text
客户端
    → 宿主机 IP:8080
    → Netfilter DNAT
    → 容器 IP:80
    → Linux Bridge
    → veth Pair
    → 容器服务
```

DNAT 修改入站数据包的目标地址和端口，转发规则允许数据包通过，Conntrack 则保证响应沿正确的反向转换路径返回。根据访问来源和 Docker 配置，实际实现还可能涉及额外的 Netfilter 规则或用户态代理。

Port Mapping 与 Conntrack 的职责不同：

- Port Mapping 预先定义“宿主机的哪个端口转到哪个容器服务”。
- Conntrack 动态记录每一条实际连接及其转换状态。

如果 `-p` 没有指定宿主机 IP，端口通常会绑定到所有宿主机地址。是否能从外部访问，还会受到宿主机防火墙、路由和云平台安全规则的影响。

## 6. Docker 如何组合这些机制

典型的 Linux Bridge 网络可以画成：

```text
                         Internet
                            │
                    宿主机外部网络接口
                            │
                  IP Routing / Netfilter NAT
                            │
             Linux Bridge（例如 docker0）
                     /               \
               host veth          host veth
                   │                  │
               veth Pair          veth Pair
                   │                  │
          Container A NetNS   Container B NetNS
             eth0                 eth0
          172.17.0.2           172.17.0.3
```

三种典型通信场景可以整理为：

| 场景 | 主要机制 |
|---|---|
| 容器 A → 容器 B | veth Pair + Linux Bridge |
| 容器 A → Internet | veth Pair + Bridge + 路由 + MASQUERADE/SNAT |
| Internet → 容器 A 服务 | Port Mapping + DNAT + 路由 + Bridge + veth Pair |

它们分别回答了三个问题：

```text
NetNS 隔离后怎样接出去？       → veth Pair
多个容器怎样接入同一个网络？   → Linux Bridge
内部地址怎样访问或被外部访问？ → 路由、NAT 与端口发布
```

在 macOS 的 Docker Desktop 上，容器和这些 Linux 网络机制运行在隐藏的 Linux 虚拟机中。macOS 到虚拟机、再到容器的端口访问还会经过 Docker Desktop 提供的转发层，因此不能直接把 macOS 当作原生 Linux Docker 宿主机观察所有接口和 Netfilter 规则。

## 7. 与 AWS VPC 网络的关联

Linux 容器网络和 AWS VPC 不在同一个实现层级，但可以通过它们解决的问题建立联系：

| AWS 组件 | 主要作用 |
|---|---|
| NAT Gateway | 让私有子网中的资源主动访问外部 IPv4 网络，并接收对应响应 |
| Internet Gateway | 作为 VPC 路由表中的互联网目标；满足地址和路由条件时支持公网通信 |
| ALB / NLB | 通过监听器接收入站连接，并将流量分发给目标组中的后端服务 |
| Interface Endpoint | 通过 PrivateLink 和子网中的私有 IP 访问支持的服务 |
| Gateway Endpoint | 通过路由表私有访问 S3、DynamoDB |

### NAT Gateway 与 Docker 出站 NAT

二者都解决“内部私有地址主动访问外部网络”的问题，并允许响应流量返回。但 AWS NAT Gateway 是云平台托管的网络服务，不是 Docker 在单台 Linux 主机上配置的 Netfilter 规则。

NAT Gateway 不提供任意的公网入站端口转发。需要公开服务时，AWS 通常使用带公网地址的资源或 ALB、NLB 等入口。

### Internet Gateway 不是单独生效的开关

把 Internet Gateway 连接到 VPC 并不代表所有实例立即可以访问互联网。还需要正确的路由表；对于 IPv4，实例通常还需要公有 IPv4 地址或 Elastic IP。安全组和网络 ACL 也必须允许相应流量。

### VPC Endpoint 与 Port Mapping

Interface Endpoint 不是公网服务入口。它通过子网中的弹性网络接口和私有 IP，让 VPC 内的客户端私有访问 AWS 服务或 PrivateLink 服务。

Gateway Endpoint 则通过路由表把访问 S3、DynamoDB 的流量导向端点。两类 VPC Endpoint 都是在避免经过公网入口或 NAT；它们与“把宿主机端口转发到容器端口”的 Port Mapping 不是同一种机制。

## 我现在的理解

Linux 容器网络是一套由内核虚拟设备和网络功能组合出来的基础设施：

```text
Network Namespace  ≈ 独立的网络上下文
veth Pair          ≈ 虚拟网线及其两端接口
Linux Bridge       ≈ 二层交换机
Routing + NAT      ≈ 网关的路由和地址转换
Port Mapping       ≈ 预先配置的入站端口转发
```

容器不需要独占物理网卡，也不需要独立的 Linux 内核，就能拥有独立的 IP、路由和端口空间。Docker 的作用，是把这些 Linux 机制按照容器生命周期自动创建、连接、配置并回收。

## 延伸阅读

- [Linux network namespaces](https://man7.org/linux/man-pages/man7/network_namespaces.7.html)
- [Linux veth](https://man7.org/linux/man-pages/man4/veth.4.html)
- [Linux 内核 Ethernet Bridge 文档](https://docs.kernel.org/networking/bridge.html)
- [Docker：Port publishing and mapping](https://docs.docker.com/engine/network/port-publishing/)
- [AWS：Internet Gateway](https://docs.aws.amazon.com/vpc/latest/userguide/VPC_Internet_Gateway.html)
- [AWS：NAT Gateway](https://docs.aws.amazon.com/vpc/latest/userguide/nat-gateway-basics.html)
- [AWS：Gateway Endpoint](https://docs.aws.amazon.com/vpc/latest/privatelink/gateway-endpoints.html)
