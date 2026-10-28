# Linux 文件系统、Mount 与 Mount Namespace 学习心得

[English](../where-is-container-root-filesystem.md) | **中文** | [日本語](../ja/where-is-container-root-filesystem.md)

## 1. 对 Linux 目录树的重新认识

过去习惯于 Windows 资源管理器的图形化视角，很容易形成一种认知：操作系统中存在一棵固定的目录树，所有进程都在这棵树中访问和操作文件。

但 Linux 的实际模型并非如此：

- 根目录 `/` 不等于某块物理磁盘。
- 一棵完整的目录树可以由多个文件系统组合而成。
- 不同进程不一定看到相同的目录树。
- 某些目录对应磁盘上的真实数据，另一些目录则由内核动态提供内容。

例如：

```text
/
├── etc/      普通文件系统
├── home/     普通文件系统
├── proc/     procfs 虚拟文件系统
└── sys/      sysfs 虚拟文件系统
```

因此，Linux 的目录树是内核通过文件系统和挂载机制组织出的逻辑视图，而不是一棵固定存在的物理树。

## 2. Mount 与 Mount Namespace

### Mount（挂载）

Mount 的作用是将一个文件系统接入现有目录树的某个位置。

例如：

```bash
mount /dev/sdb1 /data
```

这条命令表示将 `/dev/sdb1` 上的文件系统挂载到 `/data`。此后访问 `/data` 时，内核就会按照这条挂载关系进入对应的文件系统。

挂载不会复制文件内容，而是建立目录树中的路径与文件系统之间的关联。

### Mount Namespace（挂载命名空间）

Mount Namespace 可以理解为一组进程共享的挂载关系视图。

Linux 宿主机上的普通进程通常位于初始 Mount Namespace 中，容器进程则可以使用独立的 Mount Namespace：

```text
Namespace A          Namespace B
    /                     /
    └── data/             └── data/
         │                     │
     文件系统 X             文件系统 Y
```

两个进程访问相同的 `/data` 路径，却可以进入不同的文件系统。

需要注意：

- Namespace 不会自动复制底层文件系统。
- Namespace 不会自动改变进程的根目录。
- 不同 Namespace 可以引用相同的底层文件资源。
- 挂载事件是否传播到其他 Namespace，还取决于 `shared`、`private` 等挂载传播属性。

## 3. 进程的 root 与 cwd

Linux 内核为进程维护两个重要的文件系统上下文属性：

- `root`：绝对路径解析的起点。
- `cwd`：相对路径解析的起点。

它们通常从父进程继承。

例如：

```bash
cd /home/user
```

这会改变 Shell 进程的 `cwd`。

而下面的系统调用会将进程的 `root` 改为 `/some/rootfs`，使它之后解析绝对路径时从这里开始：

```c
chroot("/some/rootfs");
```

`chroot` 只改变路径解析的起点，本身不是完整的安全隔离机制。容器运行时还会结合 Mount Namespace、挂载配置、权限限制以及 `pivot_root` 等机制准备容器环境。

因此，需要区分三个独立概念：

```text
root             绝对路径解析起点
cwd              相对路径解析起点
Mount Namespace  可见的文件系统挂载关系
```

即使两个进程属于同一个 Mount Namespace，也可以拥有不同的 `root` 和 `cwd`。反过来，即使两个进程都把自己的根目录称为 `/`，它们看到的文件系统内容也可能不同。

## 4. OverlayFS 与 Docker 容器根目录

Overlay 的含义是叠加、覆盖。OverlayFS 可以将多个目录层合并成统一的文件系统视图。

它主要包含：

- `lowerdir`：一个或多个基础层，通常作为只读层使用。
- `upperdir`：可写层，保存新增和修改的文件。
- `workdir`：OverlayFS 内部操作所需的工作目录。
- `merged`：挂载完成后呈现给使用者的合并视图。

```text
lowerdir ──┐
           ├── OverlayFS ── merged ── 容器 /
upperdir ──┘
```

查找文件时，OverlayFS 会先考虑 `upperdir`，再查找 `lowerdir`。如果上下层存在同名目录，它们还可能被合并显示。

当容器需要修改只存在于 `lowerdir` 中的文件时，OverlayFS 通常先把文件复制到 `upperdir`，再修改上层副本。这就是 Copy-on-Write（写时复制）。

Docker 容器通常拥有各自的可写层，因此多个容器可以共享镜像基础层，同时隔离各自的修改。容器进程看到的是 OverlayFS 合并后的目录树，不需要知道某个文件实际来自哪一层。

另外，容器中的 `/proc` 并不是复制出来的一套内核状态。容器运行时会在容器的挂载视图中挂载 procfs，同一个 Linux 内核再根据该 procfs 对应的 PID Namespace 展示进程信息。

因此，容器中的进程可以看到自己是 PID 1，而宿主机仍然使用另一个 PID 标识同一个进程。两边读取的都是内核维护的进程对象，只是看到的 PID 和进程范围不同。

## 5. 最终认知模型

Linux 内核管理着底层文件系统资源，以及将这些资源接入目录树的挂载关系。进程再通过自己的文件系统上下文解析路径：

```text
Linux 内核
    │
    ├── 底层文件系统资源
    │     ├── ext4
    │     ├── OverlayFS
    │     ├── procfs
    │     └── sysfs
    │
    └── Mount Namespace
          │
          ├── 挂载关系视图 A
          │      └── 进程 A（root / cwd）
          │
          └── 挂载关系视图 B
                 └── 进程 B（root / cwd）
```

关键认识：

1. 目录树不是所有进程必须共享的一棵固定物理树。
2. Mount 负责建立文件系统与目录树之间的挂载关系。
3. Mount Namespace 让不同进程组拥有不同的挂载关系视图。
4. `root` 和 `cwd` 决定进程解析路径的起点。
5. Docker 将 Mount Namespace、根目录切换和 OverlayFS 等机制结合起来，为容器进程提供独立的文件系统环境。

**容器的文件系统隔离，本质上不是复制一台完整的计算机，而是通过同一个 Linux 内核，为不同进程组织出不同的文件系统视图。**
