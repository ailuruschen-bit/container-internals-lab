# Docker Compose 状态观察、重启与资源限制

[English](../17-docker-compose-state-and-resource-limits.md) | **中文** | [日本語](../ja/17-docker-compose-state-and-resource-limits.md)

理解 Compose 的基本配置后，我开始关注另一个问题：容器创建出来以后，怎样观察它们是否正常运行，以及怎样限制它们对宿主机资源的使用。

## 1. 从 Project 到 Container 观察状态

查看当前 Docker Engine 中正在运行的 Compose Project：

```bash
docker compose ls
```

本次实验可以看到：

```text
NAME         STATUS       CONFIG FILES
05-compose   running(2)   .../05-compose/docker-compose.yaml
```

`running(2)` 表示这个 Compose Project 当前有两个运行中的 Container。它是项目级别的概要，并不能说明每个容器内部的服务都已经可用。

继续向下观察实际 Container：

```bash
docker ps
```

```text
05-compose-app-1   Up 8 days
05-compose-db-1    Up 8 days (healthy)
```

这里可以看到 Compose Service 最终创建的 Container，以及运行时长和健康状态。

几个命令的观察范围不同：

```text
docker compose ls      当前 Engine 中的 Compose Project
docker compose ps      当前 Compose Project 的 Container
docker ps              当前 Engine 中所有运行中的 Container
docker ps -a           当前 Engine 中包括已停止实例在内的全部 Container
```

## 2. `restart`：Container 退出后的基础恢复

Compose 可以为 Service 设置重启策略：

```yaml
services:
  app:
    image: spring-lab:dev
    restart: unless-stopped
```

常见策略包括：

| 配置 | 含义 |
|---|---|
| `no` | 默认值，不自动重启 |
| `on-failure` | 主进程以非零状态退出时重启 |
| `always` | Container 停止后持续尝试重启 |
| `unless-stopped` | 自动重启，但被明确停止或删除后不再自动拉起 |

我目前最常用的是 `unless-stopped`。它适合需要长期运行，同时又希望保留人工停止意图的服务。

不过，`restart` 关注的是 **Container 是否已经停止**。Container 的主进程仍在运行时，即使内部服务已经无法正常响应，重启策略也不会仅凭这一点触发。

## 3. `healthcheck`：检查内部服务是否可用

`healthcheck` 可以定期在 Container 内执行一条检查命令：

```yaml
services:
  db:
    image: postgres:17-alpine
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U $$POSTGRES_USER -d $$POSTGRES_DB"]
      interval: 5s
      timeout: 5s
      retries: 10
```

Docker 会根据检查结果维护 Container 的健康状态：

```text
starting ──→ healthy
     │
     └────→ unhealthy
```

检查命令在目标 Container 内执行，因此 Image 中必须存在 `pg_isready`、`curl` 或其他被调用的程序。

在依赖方使用：

```yaml
services:
  app:
    depends_on:
      db:
        condition: service_healthy
```

Compose 会在首次启动时等待 `db` 通过健康检查，再启动 `app`。

需要特别区分：

```text
restart                         Container 退出后是否重新启动
healthcheck                     运行中的 Container 是否健康
depends_on + service_healthy    依赖方首次启动前是否等待健康
```

Container 在运行过程中变成 `unhealthy`，Docker 不会因为健康状态本身自动重启它；`depends_on` 也不会在此时重新执行一次启动编排。怎样恢复这类运行中故障，留到后面的完整项目实验中再结合具体场景讨论。

## 4. 用 Cgroup 限制 CPU 与内存

Container 默认没有 CPU 和内存上限，可能与宿主机上的其他进程竞争大量资源。Compose 可以把资源约束交给 Docker，最终由 Linux Cgroup 执行限制：

```yaml
services:
  app:
    cpus: 1.0
    mem_limit: 512m

  db:
    cpus: 0.5
    mem_limit: 256m
```

`cpus` 表示允许使用的 CPU 计算配额：

```text
cpus: 0.5   最多约等于半个逻辑 CPU 的计算时间
cpus: 1.0   最多约等于一个逻辑 CPU 的计算时间
```

它限制的是一段时间内可获得的总 CPU 时间，并不是把 Container 固定到某一个 CPU Core。需要绑定具体 Core 时是另一项 `cpuset` 配置。

`mem_limit` 是 Container 可以分配的内存上限。超过硬限制后，内存分配可能失败，进程也可能被 OOM 机制终止。如果 Container 因此退出，前面的重启策略才可能参与后续恢复。

资源限制的目的不只是保护宿主机，也是在部署前明确应用所需资源。限制值过低同样会制造不稳定，因此需要结合实际监控逐步调整。

## 5. 用 `docker stats` 观察资源使用

持续观察所有运行中 Container：

```bash
docker stats
```

只观察指定 Container：

```bash
docker stats 05-compose-app-1
```

该命令默认持续刷新，按 `Ctrl+C` 退出。如果只需要当前的一次采样：

```bash
docker stats 05-compose-app-1 --no-stream
```

主要字段包括：

| 字段 | 含义 |
|---|---|
| `CPU %` | 当前 CPU 使用率 |
| `MEM USAGE / LIMIT` | 当前内存使用量与上限 |
| `MEM %` | 已使用内存占上限的比例 |
| `NET I/O` | 网络收发量 |
| `BLOCK I/O` | 块设备读写量 |
| `PIDS` | Container 内的进程与线程数量 |

本次实验在尚未配置 `mem_limit` 时，两个 Container 显示的内存上限都接近 Docker Linux 环境的可用内存。重新创建带有限制的 Container 后，`MEM USAGE / LIMIT` 中的 LIMIT 才会变成 Compose 中指定的值。

修改 `compose.yaml` 后可以执行：

```bash
docker compose up -d
docker stats --no-stream
```

Compose 会根据配置变化重新创建相关 Container，命名 Volume 中的数据仍然保留。

## 6. 当前形成的理解

```text
Compose Project 状态
    └── docker compose ls

Container 运行状态
    ├── docker compose ps / docker ps
    ├── restart 处理进程退出
    └── healthcheck 描述服务健康

资源边界
    ├── cpus / mem_limit 写入 Cgroup 约束
    └── docker stats 观察实际用量
```

`restart`、`healthcheck` 和资源限制解决的是三类不同问题：容器退出后的恢复、服务可用性的观察，以及宿主机资源的保护。它们组合起来提供了最基础的状态管理能力，但还不等同于完整的故障检测与自动恢复系统。
