# Namespaces Isolate the View; Cgroups Limit the Usage

**English** | [中文](zh/namespace-views-and-cgroup-limits.md) | [日本語](ja/namespace-views-and-cgroup-limits.md)

Once I understood namespaces, a question came up: if a container already cannot see the processes, filesystems, and network outside it, why does it still need cgroups?

The answer is that the two solve different kinds of problems:

```text
Namespace  decides which resources a group of processes can see and act on
Cgroup     accounts for and limits how much of a resource a group of processes can use
```

## Namespaces Isolate the View of Resources

The PIDs, mount tree, network devices, hostname, and so on that a process sees are, to begin with, abstractions that the Linux kernel presents to the process. Namespaces let different processes use different views:

- A PID namespace lets a container see only the process IDs within its own scope.
- A mount namespace gives a container its own set of mount relationships.
- A network namespace gives a container its own network interfaces, IP addresses, and routing table.

This isolation is not merely a matter of changing what gets displayed. For example, the routing tables and virtual network interfaces in different network namespaces really are separate resource instances maintained by the kernel. Even so, they are still managed by the same Linux kernel, and the underlying CPU and memory still come from the same machine.

So even if a container cannot see any other process on the host, it can still exhaust the entire machine's memory or monopolize the CPU for long periods. Namespaces do nothing to prevent that.

## A Cgroup Is a Resource Account for Processes

Cgroup is short for *control group*. Cgroups organize processes into groups, and individual controllers then account for, schedule, and limit the resources those processes use.

Common controllers include:

| Controller | Purpose |
|---|---|
| `cpu` | Sets CPU scheduling weights or time quotas |
| `memory` | Accounts for and limits memory usage |
| `pids` | Limits the number of processes and threads |
| `io` | Accounts for and limits block-device I/O |
| `cpuset` | Specifies which CPUs and memory nodes may be used |

For example, `memory.max=512M` does not mean that a dedicated block of memory is carved out in advance; it means the combined memory usage of the processes in the group must not exceed this ceiling. A cgroup is better thought of as a resource account with usage tracking and budget rules than as a scaled-down virtual computer.

## Cgroup v2 Is a Single Unified Tree

Modern Linux primarily uses cgroup v2, in which all cgroup nodes form a single, unified hierarchy:

```text
/
├── system.slice
│   ├── docker.service
│   │   ├── container-a  ← one group of container processes
│   │   └── container-b  ← another group of container processes
│   └── ssh.service
└── user.slice
    └── user-1000.slice
        └── shell        ← shell process
```

In the standard cgroup v2 process model, each process belongs to exactly one cgroup node in this tree at any given time. When a process creates a child, the child initially inherits its parent's cgroup. Writing a PID to another node's `cgroup.procs` file moves the entire process to that node:

```bash
echo 1234 > /sys/fs/cgroup/service-b/cgroup.procs
```

Although a process belongs directly to only one node, it is constrained simultaneously by the rules of that node and of all its ancestors:

```text
apps                 all applications combined may use at most 4 GB
└── service-a        this service may use at most 1 GB
    └── process 1234
```

A child node can only tighten a limit further; it can never exceed a ceiling already set by an ancestor.

To prevent processes in a parent node from competing with its child nodes for the same resources, cgroup v2 also imposes a structural rule for regular domain controllers: before a non-root node can delegate a controller to its children, it generally must first move its own processes into child nodes. In practice, therefore, management trees tend to place processes in leaf nodes and use intermediate nodes for organizing and distributing resources.

## A Node Needs Only Some of the Controllers

Controllers are resource-control features provided by the kernel; they do not belong to any particular cgroup node. Each cgroup node determines which controllers it can use and which ones it passes down to its children.

Two files are easily confused:

```text
cgroup.controllers       controllers available in this node, which it may pass down
cgroup.subtree_control   controllers this node has actually enabled for its direct children
```

For example, suppose the root node supports:

```text
cpu memory pids io
```

It can still choose to enable only some of them downward:

```bash
echo "+cpu +memory" > /sys/fs/cgroup/cgroup.subtree_control
```

The direct children can then configure CPU and memory control, but they cannot use the `pids` and `io` controllers, which have not been passed down, anywhere in their subtrees. Resource control follows a top-down rule: a child cannot enable a controller that its parent has not made available to it.

Therefore:

```text
A process belongs to a cgroup
        ≠
Every resource has a concrete limit
```

A process can belong to `/app/service`, but if `memory.max` is not set anywhere along that path, the cgroup tree imposes no hard memory ceiling on it.

## What `/proc/self/cgroup` Shows

You can check which cgroup the current process belongs to:

```bash
cat /proc/self/cgroup
```

In my lab environment, this printed:

```text
0::/.lxc
```

Each line has the format:

```text
hierarchy ID : controller list : cgroup path
```

In cgroup v2, all controllers share a single unified hierarchy, so the line usually looks like this:

```text
0::<path>
```

The `0` denotes the unified hierarchy, the middle field no longer lists individual controllers, and `/.lxc` is the path as seen from the current cgroup namespace. The full path seen from the outer host may be longer.

Cgroup v1 allows a separate hierarchy for each controller, so a single process may appear on several lines in `/proc/self/cgroup`, with different paths in the CPU, memory, and other hierarchies. Cgroup v2 is precisely what unified these controllers into a single tree.

## How Docker Uses Cgroups

When Docker creates a container, it places the container's processes in a corresponding cgroup and translates the launch options into kernel resource rules:

```bash
docker run \
    --memory=512m \
    --cpus=0.5 \
    --pids-limit=100 \
    nginx
```

Under cgroup v2, the options map roughly as follows:

```text
Docker option        Cgroup interface
--memory=512m   →    memory.max
--cpus=0.5      →    cpu.max
--pids-limit    →    pids.max
```

Docker does not reimplement CPU scheduling or memory limits. After organizing the container's processes, it configures the cgroup mechanisms that the Linux kernel already provides.

## My Current Understanding

Namespaces and cgroups can be told apart like this:

```text
Namespace  isolates or virtualizes the view of resources that processes see
Cgroup     accounts for, schedules, and caps the actual resource usage of a process group
```

Combined, they make a container's processes look as if they live in an independent environment, while preventing them from consuming the host's resources without limit. Yet they remain ordinary processes managed by the same Linux kernel.

One boundary is worth keeping in mind: namespaces and cgroups are not a complete security model. Containers usually also rely on mechanisms such as the root filesystem (rootfs), capabilities, and seccomp to further restrict which files processes can access and which operations they can perform.
