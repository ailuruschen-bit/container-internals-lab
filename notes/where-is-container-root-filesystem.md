# 容器里的 `/`，到底在哪里？

理解容器文件系统时，最重要的变化不是磁盘上突然多出了一套特殊文件，而是**同一个 Linux 内核为不同进程组织出了不同的路径视图**。

Docker 会组合 PID、Mount、Network、UTS 等 Namespace，让容器进程看到一套看似独立的系统环境。其中，决定进程能看到哪些挂载、以及这些挂载怎样组成目录树的，是 Mount Namespace。

## 隔离的是挂载视图

每个进程都有自己的根目录和当前工作目录。路径解析从进程的根目录开始，再沿着它所在 Mount Namespace 中的挂载关系向下查找。

因此，容器里的 `/etc/hosts` 和宿主机进程看到的 `/etc/hosts` 虽然路径相同，却可以来自不同的挂载。反过来，同一份文件也可以通过 bind mount，在宿主机和容器中以两条完全不同的路径出现。

Mount Namespace 隔离的是“这棵挂载树长什么样”，不是复制出一个新的内核或一块新的物理磁盘。

## 容器的根文件系统不是一个普通目录那么简单

容器启动前，运行时会先准备一棵根文件系统，再把它设为容器进程看到的 `/`。这棵目录树通常由多种挂载共同组成：

```text
镜像只读层 ─┐
镜像只读层 ─┼─ OverlayFS 等存储机制 ─→ 合并后的根文件系统 `/`
容器可写层 ─┘                              ├── bind mount / volume
                                             ├── procfs 挂载到 /proc
                                             └── tmpfs 等其他挂载
```

以常见的 OverlayFS 为例，多个只读镜像层是 `lowerdir`，容器自己的可写层是 `upperdir`，内核把它们合并成一个 `merged` 视图。容器进程从 `/` 向下看时，不需要知道某个文件来自哪一层。

这些层的底层数据确实保存在 Docker daemon 所在的主机上，但不能简单理解为“容器的 `/` 永远对应宿主机上的一个普通目录”：

- 不同存储驱动或 snapshotter 的组织方式不同；
- OverlayFS 的一个可见目录可能由多个目录合并而成；
- 读取文件时可能直接使用只读层，第一次修改时则可能发生 copy-up，文件被复制到可写层后再修改；
- volume、bind mount 和 tmpfs 又有各自独立的来源。

Docker 的内部存储目录由 Docker 管理，不应通过直接修改 `/var/lib/docker` 中的文件来改变容器内容。

## 什么时候可以说“两条路径是同一份文件”？

Bind mount 是最直观的情况：宿主机上的一个文件或目录被挂到容器中的另一个路径。此时两边通过不同路径访问同一份挂载内容，容器内的写入也会反映到宿主机，除非挂载被设为只读。

```bash
mkdir -p demo-data
printf 'from host\n' > demo-data/message.txt

docker run --rm \
    --mount "type=bind,src=$PWD/demo-data,dst=/data" \
    alpine sh -c 'printf "from container\n" >> /data/message.txt'

cat demo-data/message.txt
```

输出会同时包含 `from host` 和 `from container`。容器中的 `/data/message.txt` 与当前目录下的 `demo-data/message.txt` 是同一份共享数据的两个路径入口。

但对容器镜像和可写层，不能一概称为“同一个物理文件”。OverlayFS 会参与路径查找和 copy-up，容器看到的是合并后的文件系统对象，而不是一个永远固定的宿主机文件路径。

## `/proc` 展示的是内核生成的视图

`/proc` 不是普通磁盘目录。读取 `/proc/123/status` 时，内核根据自己维护的进程状态动态生成内容。

在原生 Linux 主机上，容器和宿主机共享同一个内核，信息来源也是同一套内核对象；隔离发生在内核决定“当前进程可以看到什么”时。这里至少有两层机制共同参与：

- Mount Namespace 决定容器的 `/proc` 位置挂载了哪一个 procfs 实例；
- PID Namespace 决定该 procfs 中可以看到哪些进程，以及这些进程使用什么 PID。

因此，一个容器中的主进程可以认为自己是 PID 1，而宿主机仍然用另一个 PID 标识同一个进程。容器内的 `/proc` 只列出对应 PID Namespace 可见的进程，并不是复制了一份进程数据。

```text
同一个内核中的进程对象
          │
          ├── 宿主机的 procfs → 展示宿主机可见的 PID
          │
          └── 容器的 procfs   → 展示容器 PID Namespace 可见的 PID
```

不过，不能由此推断 `/proc` 中所有信息都天然完成了隔离。不同文件可能依赖 PID、Network、Mount、User 等不同 Namespace，也可能暴露尚未虚拟化的系统级信息。容器运行时还需要正确挂载并限制 `/proc`，不能只创建一个 PID Namespace 就认为隔离已经完成。

## 我现在的理解

容器文件系统可以分成三层来看：

1. **Mount Namespace** 决定进程看到怎样的挂载树；
2. **OverlayFS、volume 和 bind mount** 决定每个路径背后的数据来自哪里；
3. **procfs 等虚拟文件系统** 由内核动态生成内容，再结合相关 Namespace 提供不同视图。

所以，容器并不是拥有一块独立磁盘，而是同一个内核为一组进程准备了一棵独立的路径树。树中的某些节点来自镜像层，某些节点直接共享宿主机文件，还有一些节点根本没有对应的磁盘文件。

## 延伸阅读

- [Linux mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
- [Linux PID namespaces 与 `/proc`](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html)
- [Linux 内核 OverlayFS 文档](https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html)
- [Docker 存储驱动与镜像层](https://docs.docker.com/engine/storage/drivers/)
- [Docker bind mounts](https://docs.docker.com/engine/storage/bind-mounts/)

> 在 macOS 上使用 Docker Desktop 时，Docker daemon 和容器运行在 Linux 虚拟机中。镜像层和容器可写层首先位于这台虚拟机里；Docker Desktop 再额外提供文件共享机制，让 macOS 路径能够作为 bind mount 出现在容器中。
