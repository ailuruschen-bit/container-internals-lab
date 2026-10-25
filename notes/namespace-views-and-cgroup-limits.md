# Namespace 隔离视图，Cgroup 限制用量

**中文** | [English](en/namespace-views-and-cgroup-limits.md) | [日本語](ja/namespace-views-and-cgroup-limits.md)

理解 Namespace 之后，我产生了一个问题：既然容器已经看不到外部的进程、文件系统和网络环境，为什么还需要 Cgroup？

答案是，两者解决的并不是同一类问题：

```text
Namespace  决定一组进程能够看到和操作哪些资源
Cgroup     统计并限制一组进程能够使用多少资源
```

## Namespace 隔离的是资源视图

进程看到的 PID、挂载树、网络设备、主机名等内容，本来就是 Linux 内核为进程提供的抽象视图。Namespace 让不同进程使用不同的视图：

- PID Namespace 让容器只看到自身范围内的进程编号。
- Mount Namespace 让容器看到独立的挂载关系。
- Network Namespace 让容器拥有独立的网卡、IP 和路由表。

这种隔离不只是修改显示结果。例如 Network Namespace 中的路由表和虚拟网卡，确实是内核分别维护的资源实例。不过，它们仍然由同一个 Linux 内核管理，底层 CPU 和内存也仍然来自同一台机器。

所以，一个容器即使看不到宿主机上的其他进程，也可能耗尽整台机器的内存或长期占用 CPU。Namespace 没有解决这个问题。

## Cgroup 是进程的资源账户

Cgroup 是 Control Group 的缩写。它把进程组织成组，再由不同的 controller 对这些进程进行资源统计、调度和限制。

常见的 controller 包括：

| Controller | 作用 |
|---|---|
| `cpu` | 设置 CPU 调度权重或时间配额 |
| `memory` | 统计并限制内存使用 |
| `pids` | 限制进程和线程数量 |
| `io` | 统计并限制块设备 I/O |
| `cpuset` | 指定可以使用哪些 CPU 和内存节点 |

例如，`memory.max=512M` 的含义不是提前划出一块专属内存，而是这组进程的内存使用量合计不能超过这个上限。Cgroup 更像一个带有统计和预算规则的资源账户，而不是一台缩小的虚拟计算机。

## Cgroup v2 是一棵统一的树

现代 Linux 主要使用 Cgroup v2。所有 Cgroup 节点组成一棵统一的层级树：

```text
/
├── system.slice
│   ├── docker.service
│   │   ├── container-a  ← 一组容器进程
│   │   └── container-b  ← 另一组容器进程
│   └── ssh.service
└── user.slice
    └── user-1000.slice
        └── shell        ← Shell 进程
```

在普通的 Cgroup v2 进程模型中，每个进程在同一时刻只属于这棵树中的一个 Cgroup 节点。进程创建子进程时，子进程最初继承父进程的 Cgroup；把 PID 写入另一个节点的 `cgroup.procs`，则会把整个进程移动过去。

```bash
echo 1234 > /sys/fs/cgroup/service-b/cgroup.procs
```

进程虽然只直接属于一个节点，却会同时受到当前节点和祖先节点规则的约束：

```text
apps                 所有应用合计最多使用 4 GB
└── service-a        当前服务最多使用 1 GB
    └── 进程 1234
```

子节点只能进一步收紧限制，不能突破祖先已经设定的上限。

为了避免父节点中的进程与子节点竞争同一份资源，Cgroup v2 对普通 domain controller 还有一条结构规则：非根节点如果要把 controller 下放给子节点，通常需要先把自己的进程移动到子节点中。因此实际的管理树常把进程放在叶子节点，把中间节点用于组织和分配资源。

## 一个节点只需要启用部分 Controller

Controller 是内核提供的资源控制功能，不从属于某一个 Cgroup 节点。Cgroup 节点决定自己能够使用哪些 controller，以及把哪些 controller 继续提供给子节点。

两个文件容易混淆：

```text
cgroup.controllers       当前节点有哪些 controller 可供下放
cgroup.subtree_control   当前节点实际向直接子节点启用了哪些 controller
```

例如，根节点支持：

```text
cpu memory pids io
```

但它可以只向下启用：

```bash
echo "+cpu +memory" > /sys/fs/cgroup/cgroup.subtree_control
```

这样直接子节点可以配置 CPU 和内存控制，却不能在自己的子树中使用尚未下放的 `pids` 和 `io` controller。资源控制遵循自上而下的规则：父节点没有提供的 controller，子节点不能凭空启用。

因此：

```text
进程有 Cgroup 归属
        ≠
所有资源都设置了具体限额
```

一个进程可以属于 `/app/service`，但如果沿途没有设置 `memory.max`，它就没有一个由该 Cgroup 树指定的内存硬上限。

## `/proc/self/cgroup` 表示什么

可以查看当前进程的 Cgroup 归属：

```bash
cat /proc/self/cgroup
```

我在实验环境中看到的是：

```text
0::/.lxc
```

每一行的格式是：

```text
层级 ID : Controller 列表 : Cgroup 路径
```

在 Cgroup v2 中，所有 controller 使用统一层级，因此通常显示为：

```text
0::<path>
```

`0` 表示统一层级，中间字段不再列出分散的 controller，`/.lxc` 是当前 Cgroup Namespace 视角下的路径。外层宿主机看到的完整路径可能更长。

Cgroup v1 可以为不同 controller 建立不同层级，因此一个进程可能在 `/proc/self/cgroup` 中出现多行，并在 CPU、Memory 等层级中拥有不同路径。Cgroup v2 正是把这些 controller 统一到了一棵树中。

## Docker 如何使用 Cgroup

Docker 创建容器时，会把容器进程放入对应的 Cgroup，并将启动参数转化为内核资源规则：

```bash
docker run \
    --memory=512m \
    --cpus=0.5 \
    --pids-limit=100 \
    nginx
```

在 Cgroup v2 中，可以建立大致对应关系：

```text
Docker 参数          Cgroup 控制接口
--memory=512m   →    memory.max
--cpus=0.5      →    cpu.max
--pids-limit    →    pids.max
```

Docker 没有重新实现 CPU 调度和内存限制，而是在组织好容器进程后，配置 Linux 内核已有的 Cgroup 机制。

## 我现在的理解

Namespace 与 Cgroup 可以这样区分：

```text
Namespace  隔离或虚拟化进程看到的资源视图
Cgroup     对进程组的真实资源使用进行统计、调度和限额
```

两者结合后，容器中的进程既像处于独立环境中，又不能无限制地消耗宿主机资源。但它们仍然只是同一个 Linux 内核管理的普通进程。

还需要保留一个边界：Namespace 和 Cgroup 不是完整的安全体系。容器通常还会结合 rootfs、Capabilities、Seccomp 等机制，进一步约束进程能够访问的文件和执行的操作。
