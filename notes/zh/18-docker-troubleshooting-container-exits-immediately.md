# Docker 故障排查实操

[English](../18-docker-troubleshooting-container-exits-immediately.md) | **中文** | [日本語](../ja/18-docker-troubleshooting-container-exits-immediately.md)

遇到容器故障，我先用 `ps` 确认容器是否还在运行，再看日志和 `inspect`。这几个实验的起点相同，接下来查什么取决于眼前的症状。

## 先看状态和日志

```bash
docker ps -a
docker logs --tail=100 <container>
docker compose logs --tail=100 <service>
```

在 Compose 文件所在目录用 `docker compose ps -a` 看当前项目。普通 `docker` 命令接收容器名或 ID，`docker compose logs`、`exec` 和 `top` 接收服务名。排查单副本服务时可以按手头的名字选用。

## 1. 容器启动后立即退出

容器跟着主进程退出。`Exited (0)` 可能只是一次性任务正常结束。异常退出时，我先看 `.State` 里的 `ExitCode`、`Error` 和起止时间，再用 `.Config.Entrypoint` 与 `.Config.Cmd` 核对启动配置。应用日志可能来不及写出，启动阶段的问题也可能只出现在 `Error` 里。

```bash
docker inspect -f '{{json .State}}' <container>
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

不熟悉字段路径时，可以先看完整的 `docker inspect <container>`。它输出 JSON 数组，后面用 `jq` 时写 `.[0]` 就是取第一个容器对象。

## 2. 容器还在运行，服务却挂了

```bash
docker logs -f --tail=20 <container>
docker top <container>
docker exec -it <container> sh
```

容器显示 `Up` 时，我会跟随最近的日志，再用 `top` 从外面确认服务进程是否还活着。需要查文件、权限或监听情况时，用 `exec` 进入容器，再运行镜像里已有的 `ps` 等工具。服务进程还在时，可以从容器内测试监听端口，缩小访问失败的范围。

在容器里验证了修复后，把改动写回项目。改了 `compose.yaml` 就用 `docker compose up -d app` 应用配置。`docker compose restart app` 会沿用现有容器的配置。改了镜像构建内容则用 `docker compose up -d --build app`，再验证原先失败的请求。

## 3. Spring Boot 解析不到数据库主机名

Spring Boot 连不上 PostgreSQL 时，我会顺着日志里的 `Caused by` 找原因。看到 `UnknownHostException: db`，就从应用容器里运行 `getent hosts db`。原容器已经退出时，用相同服务配置启动一个临时 Shell：

```bash
docker compose exec app getent hosts db
```

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`getent hosts` 按系统配置查主机名，结果也可能来自 `/etc/hosts`。查不到 `db` 时，核对 JDBC URL、数据库容器状态，以及两个服务是否共享网络。查到 IP 后，再检查数据库端口和认证。

实测时，临时容器返回了 `db` 的 IP。不存在的名称查不到记录。

## 4. 健康检查失败

```bash
docker inspect -f '{{json .State.Health}}' <container>
docker inspect <container> | jq '.[0].State.Health'
docker inspect <container> | jq '.[0].State.Health.Log[-1]'
```

`Up (unhealthy)` 时先看 `.State.Health`。`FailingStreak` 是连续失败次数，`Log[-1]` 是最近一条检查记录。先看其中的 `ExitCode` 和 `Output`，需要复跑时从 `.Config.Healthcheck` 找到实际命令。

实验里我用一个缺失的文件让检查失败，`Output` 里直接显示了 `ready file missing`。

## 5. 容器因内存不足退出

突然 `Exited (137)` 且应用日志里没有明显异常时，我会查 OOM。`137` 通常对应 `SIGKILL`，手动强制终止也可能得到这个退出码。`OOMKilled: true` 表示 Docker 记录到了 OOM，`Error` 此时仍可能为空。

```bash
docker inspect <container> | jq '.[0].State | {Status, ExitCode, OOMKilled, Error}'
docker inspect <container> | jq '.[0].HostConfig.Memory'
```

在内存上限为 `32 MiB` 的临时容器里，我观察到了 `ExitCode: 137` 和 `OOMKilled: true`。

`.HostConfig.Memory` 的单位是字节，`0` 表示没有为这个容器单独设置内存上限。用 Compose 时可以和 `mem_limit` 对照。能复现时，在容器还运行期间用 `docker stats <container>` 观察 `MEM USAGE / LIMIT`，看内存是突然冲高还是持续增长。
