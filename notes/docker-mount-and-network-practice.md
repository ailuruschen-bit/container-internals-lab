# Docker 基础实操：把 Mount 与 Network 对应到内核机制

前面学习 Namespace、Cgroup、OverlayFS 和容器网络时，我接触到的更多是抽象机制。这次实验的目的不是背诵 Docker 命令，而是把这些机制和真正可观察的容器行为对应起来。

我的实验环境是 **macOS + OrbStack**。Docker Engine 实际运行在 OrbStack 提供的 Linux 虚拟环境中，因此有些路径会比原生 Linux Docker 多一层文件共享或端口转发。

## 1. 先认清当前运行环境

当前环境可以简化为：

```text
macOS
  └── Docker CLI
        └── Docker Context: orbstack
              └── 转发到 macOS 的 Docker Socket
                    └── OrbStack VM 中的 Docker Engine
                          └── Linux Containers
```

Docker CLI 只是客户端，Docker Context 保存客户端应该连接哪个 Engine。OrbStack Linux Machine 与 Docker Container 也是两类不同的运行环境，日常操作容器不需要先进入 Linux Machine。

我用下面几条命令确认环境：

```bash
orb start
docker context ls
docker --context orbstack info
docker version
```

我曾遇到过这样的错误：

```text
failed to connect to the docker API
... docker.sock: no such file or directory
```

这并不是 Docker 命令不存在，而是 CLI 选中的 Socket 不可用。对 OrbStack 来说，常见原因是 OrbStack 没有启动，或者当前 Context 指向了错误的位置。

## 2. 从 `run` 和 `exec` 理解容器进程

Docker 的许多命令可以按“管理对象 + 操作”来理解：

```bash
docker network create lab-net
docker network inspect lab-net
docker volume ls
```

也有一些常用简写，例如 `docker ps` 相当于 `docker container ls`。

创建容器与进入已有容器是两件不同的事：

```bash
docker run -dit --name process-lab alpine sh
docker exec -it process-lab sh
docker stop process-lab
docker rm process-lab
```

- `run` 创建一套新的容器运行环境，并启动它的主进程。
- `exec` 在正在运行的容器环境中启动一个额外进程。
- `exec` 启动的 Shell 退出，不会自动结束容器主进程。
- 容器主进程退出后，容器通常也会停止。

这里的 `alpine` 是一个轻量的 Linux 用户空间镜像，`sh` 是容器启动后执行的程序。镜像中有程序、动态库和配置文件，但没有一套独立的 Linux 内核；容器仍然使用 OrbStack Linux 环境的内核。

这次实验中最常用的参数是：

| 参数 | 含义 | 作用 |
|---|---|---|
| `-i` | interactive | 保持标准输入开放 |
| `-t` | tty | 分配伪终端 |
| `-d` | detach | 后台运行 |
| `--rm` | remove | 容器退出后自动删除 |
| `--name` | name | 指定容器名称 |
| `-u` | user | 指定运行用户 |
| `-p` | publish | 发布容器端口 |
| `--mount` | mount | 配置文件系统挂载 |
| `--network` | network | 指定 Docker 网络 |

短参数可以组合，例如 `-it` 和 `-dit`。这里的 `i` 是 Interactive，`t` 是 TTY，`d` 是 Detach，`p` 是 Publish。

## 3. Bind Mount：容器与 macOS 共享文件

我先在 macOS 上准备目录和文件：

```bash
mkdir -p "$HOME/docker-fs-lab"
echo "Hello from macOS" > "$HOME/docker-fs-lab/test.txt"
```

然后将该目录挂载到容器的 `/data`：

```bash
docker run --rm -it \
  --name mount-lab \
  --mount type=bind,source="$HOME/docker-fs-lab",target=/data \
  alpine sh
```

`--mount` 中三个字段的含义是：

- `type=bind`：使用 Bind Mount。
- `source`：挂载源，即 macOS 上的目录。
- `target`：容器中的访问路径。

在容器中读取并写入文件：

```bash
cat /data/test.txt
echo "Hello from container" > /data/container.txt
```

退出容器后，在 macOS 中仍然可以看到新文件：

```bash
cat "$HOME/docker-fs-lab/container.txt"
```

这个实验说明 `/data` 不是复制进容器的一份数据，而是容器访问共享资源的一个路径入口。在 OrbStack 中，macOS 文件先通过文件共享机制提供给 Linux 环境，再被挂载到容器中。

### Volume 与 tmpfs

除了 Bind Mount，Docker 还经常使用另外两种挂载：

| 类型 | 数据来源 | 适合场景 |
|---|---|---|
| `bind` | 指定的宿主机路径 | 开发目录、配置文件 |
| `volume` | Docker 管理的存储 | 数据库、持久化数据 |
| `tmpfs` | 内存支持的临时文件系统 | 缓存、临时数据 |

命名 Volume：

```bash
docker run --rm -it \
  --mount type=volume,source=mydata,target=/data \
  alpine sh

docker volume ls
docker volume inspect mydata
```

删除使用它的容器后，命名 Volume 仍然可以保留。与 Bind Mount 不同，它的实际存储位置由 Docker 管理。

tmpfs：

```bash
docker run --rm -it \
  --mount type=tmpfs,target=/data \
  alpine sh
```

tmpfs 不需要 `source`，容器停止后其中的数据不会作为普通持久化数据保留下来。

### 观察容器根文件系统

在容器内执行：

```bash
mount | grep ' on / '
mount | grep ' on /proc '
```

我的实验中观察到类似结果：

```text
overlay on / type overlay (...)
proc on /proc type proc (...)
```

这说明容器的 `/` 由 OverlayFS 组织，而 `/proc` 是另外挂载的 procfs。OverlayFS 通常用 `lowerdir` 提供基础层，用 `upperdir` 保存当前容器的修改，并使用 `workdir` 完成内部操作。

因此，容器看到的根目录不是一块专属物理磁盘，而是多个文件系统与挂载关系共同构成的视图。具体使用哪种存储驱动或快照机制仍取决于 Docker 环境，不能把一次实验的 `overlay` 输出当成所有环境的固定结果。

## 4. 自定义 Bridge：创建容器虚拟局域网

Docker 默认提供几种网络：

```bash
docker network ls
```

| 网络 | Driver | 作用 |
|---|---|---|
| `bridge` | bridge | 默认虚拟 Bridge 网络 |
| `host` | host | 共享 Docker 宿主环境的网络命名空间 |
| `none` | null | 不配置普通外部网络连接 |

在原生 Linux 上，`host` 指 Docker 宿主机的网络。OrbStack 在这个语义之上又做了 macOS 集成，使使用 Host Networking 的服务也能通过 Mac 的 `localhost` 访问，所以不能简单地把实验现象照搬到所有 Docker 环境。

我创建了一个自定义 Bridge 网络：

```bash
docker network create lab-net
docker network inspect lab-net
```

实验中获得的信息包括：

```text
Name: lab-net
Driver: bridge
Subnet: 192.168.97.0/24
Gateway: 192.168.97.1
```

这里的 IPAM 是 IP Address Management。Docker 会管理子网，并为接入网络的容器分配内部 IP。

创建两个容器并直接接入该网络：

```bash
docker run -dit --name net-a --network lab-net alpine sh
docker run -dit --name net-b --network lab-net alpine sh
```

其底层结构可以简化为：

```text
Linux Kernel
      │
   Bridge
    /    \
  veth  veth
   │      │
 net-a  net-b
 NetNS  NetNS
```

如果容器创建时没有接入 `lab-net`，也可以稍后连接：

```bash
docker run -dit --name net-c alpine sh
docker network connect lab-net net-c
docker network disconnect lab-net net-c
```

这里特意使用 `net-c`，因为前面的 `net-a` 已经通过 `--network lab-net` 接入，不能再把它当作“首次连接网络”的例子。一个容器也可以同时接入多个 Docker Network。

在 `net-a` 中验证与 `net-b` 的通信：

```bash
docker exec -it net-a sh
ping -c 3 net-b
```

能够直接使用 `net-b` 这个名称，是因为用户自定义 Bridge 网络提供了容器名称解析。数据则通过 veth Pair 和 Linux Bridge 真正传输。

## 5. 从容器内部观察网络配置

Alpine 缺少工具时，可以先安装：

```bash
apk add --no-cache iproute2 iputils curl netcat-openbsd
```

然后分别观察接口、路由、邻居和 DNS：

```bash
ip -4 addr show eth0
ip route
ip neigh
cat /etc/resolv.conf
```

重点不是记住输出，而是看懂它们之间的关系：

- `eth0` 是容器 Network Namespace 中的虚拟网络接口。
- 同一子网内的目标可以直接通信，其他目标通常交给默认网关。
- 邻居表记录同一链路中 IP 与 MAC 地址的对应关系。
- 自定义 Bridge 网络的 `/etc/resolv.conf` 中常能看到 Docker 内置 DNS 使用的 `127.0.0.11`。

## 6. 用 Socket 验证容器间通信

在 `net-b` 中监听 TCP 端口：

```bash
docker exec -it net-b sh
apk add --no-cache netcat-openbsd
nc -l 9000
```

在 `net-a` 中发起连接：

```bash
docker exec -it net-a sh
apk add --no-cache netcat-openbsd
nc net-b 9000
```

连接建立后，两边可以直接发送文本。通信路径是：

```text
net-a: Socket
   ↓
eth0 → veth Pair → Linux Bridge → veth Pair → eth0
                                              ↓
                                        net-b:9000
```

这个实验不需要发布宿主机端口，因为两个容器已经位于同一个虚拟网络中。

我还可以让两个容器分别监听自己的 `9000` 端口。端口属于各自的 Network Namespace，因此编号相同并不会直接冲突。

## 7. NAT：容器主动访问公网

在 `net-a` 中先验证连通性：

```bash
ping -c 3 google.com
ip -4 addr show eth0
ip route
```

普通 Bridge 网络通常已经由 Docker 配置好容器 IP、默认网关、转发和出站地址转换，因此使用私有 IP 的容器也能够访问公网。

我分别从容器与 macOS 查询公网看到的源地址：

```bash
curl -4 --noproxy '*' https://api.ipify.org
echo
```

其中 `-4` 强制使用 IPv4，`--noproxy '*'` 避免使用 curl 配置中的 HTTP 代理。

典型出站路径可以表示为：

```text
Container 私有 IP
      ↓
veth Pair
      ↓
Linux Bridge
      ↓
Routing / SNAT
      ↓
外部网络
```

SNAT 会改写源地址，必要时也会改写源端口；Conntrack 记录连接与转换状态，使返回包能够交回原容器。

不过，当前环境中可能同时存在 Docker Linux 网络、OrbStack、macOS 和家庭路由器等多层网络。公网 IP 查询只能验证最终对外呈现的地址，不能单凭这个结果断言每一层具体使用了什么 NAT 规则。

## 8. Port Mapping：从 macOS 访问容器

我创建一个 Nginx 容器，并只把服务发布到本机回环地址：

```bash
docker run -d --rm \
  --name web-lab \
  --network lab-net \
  -p 127.0.0.1:8080:80 \
  nginx:alpine
```

`127.0.0.1:8080:80` 的含义是：

| 部分 | 含义 |
|---|---|
| `127.0.0.1` | 只绑定宿主环境的本地回环地址 |
| `8080` | 对外发布的端口 |
| `80` | 容器中的服务端口 |

从 macOS 访问并查看发布关系：

```bash
curl http://localhost:8080
docker port web-lab
```

可以看到类似输出：

```text
80/tcp -> 127.0.0.1:8080
```

而位于同一个 `lab-net` 中的 `net-a` 可以直接访问容器名称和内部端口：

```bash
docker exec net-a wget -qO- http://web-lab:80
```

两条路径并不相同：

| 来源 | 请求地址 | 是否依赖本次 `-p` |
|---|---|---|
| macOS | `localhost:8080` | 是 |
| `net-a` | `web-lab:80` | 否 |

在普通 Linux Docker 宿主机上，入站端口映射常由 DNAT 等机制参与实现；在 OrbStack 中，还需要处理 macOS 与 Linux 虚拟环境之间的端口转发。

最后可以查看访问日志并停止实验容器：

```bash
docker logs web-lab
docker stop web-lab
```

由于创建时使用了 `--rm`，容器停止后会自动删除。

## 9. 从目的反推命令

经过这些实验，我更愿意先问“要改变哪一类资源”，再寻找具体参数：

| 目的 | Docker 功能 | 常用命令 |
|---|---|---|
| 访问宿主机目录 | Bind Mount | `docker run --mount` |
| 持久化应用数据 | Volume | `docker volume`、`--mount` |
| 创建虚拟局域网 | Bridge Network | `docker network create` |
| 让容器接入网络 | Network attachment | `docker run --network`、`docker network connect` |
| 查看网络配置 | Inspect | `docker network inspect` |
| 容器主动访问公网 | Routing + SNAT | 通常由普通 Bridge 网络自动配置 |
| 外部访问容器服务 | Port Mapping | `docker run -p` |
| 创建容器及主进程 | Run | `docker run` |
| 在已有容器中执行程序 | Exec | `docker exec` |
| 查看状态与输出 | Observe | `docker ps`、`docker logs`、`docker inspect` |

实验全部结束后，可以清理本次创建的容器和网络：

```bash
docker stop net-a net-b net-c
docker rm net-a net-b net-c
docker network rm lab-net
```

命名 Volume `mydata` 被设计为独立于容器保留。如果确认其中的数据不再需要，再用 `docker volume rm mydata` 删除，而不是把删除 Volume 当作容器清理的默认步骤。

## 10. 最终形成的认知

Docker 没有重新实现文件系统和 TCP/IP，而是把 Linux 已有机制组合成方便管理的容器环境：

```text
Linux Kernel
     │
     ├── Namespace：隔离 PID、Mount、Network 等资源视图
     ├── Cgroup：限制、统计和调度进程组的资源使用
     ├── Filesystem：OverlayFS、Bind Mount、Volume、tmpfs
     └── Networking：veth、Bridge、Routing、NAT、Port Mapping
```

这次实践让我把抽象概念对应到了具体行为：

- Bind Mount 说明容器可以通过新的路径访问共享文件，而不是必须复制数据。
- 根目录与 `/proc` 的挂载结果说明，容器目录树由不同文件系统共同组成。
- Ping 与 Netcat 说明，隔离的 Network Namespace 可以通过虚拟设备重新连接。
- 公网访问说明，私有 IP 可以借助路由和地址转换访问外部网络。
- Port Mapping 说明，容器不必拥有直接对外地址，也能接收从外部发起的连接。

我最终形成的学习方法是：**先明确自己希望容器获得什么环境，再找到 Docker 的配置方式，从容器内外观察结果，最后把现象还原到底层机制。**

Docker 命令不必全部背下来。知道自己正在配置文件系统、网络还是进程，再通过 `--help` 查具体参数，通常比死记命令更可靠。
