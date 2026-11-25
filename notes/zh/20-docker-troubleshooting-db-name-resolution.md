# Docker 故障排查实操（三）：Spring Boot 解析不到数据库主机名

Spring Boot 连不上 PostgreSQL 时，我还是先按平时排查 Java 应用的办法读异常链。看到 `UnknownHostException: db` 后，再从应用所在的网络环境确认 `db` 能不能解析。

## 1. 从日志里找到解析失败的主机名

```bash
docker logs --tail=100 <container>
docker compose logs --tail=100 app
```

先看最近 100 行，沿着 `Caused by` 找底层原因。比如 JDBC URL 是 `jdbc:postgresql://db:5432/labdb`，日志里如果出现 `UnknownHostException: db`，就先查 `db` 的名称解析。若日志很长，异常链只显示了一部分，可以调大 `--tail`。

## 2. 从应用容器里测试解析

如果 `app` 还在运行，可以直接在原容器里执行 `docker compose exec app getent hosts db`。如果它已经退出，就在 Compose 文件所在目录用同一个服务定义启动一个临时容器：

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`run` 根据 `app` 的配置创建一个新容器，`--entrypoint sh` 让它进入 Shell，退出后由 `--rm` 自动删除。原容器会保留，方便继续查日志。如果 `db` 已在运行，调试时可以加 `--no-deps`，跳过依赖服务的启动。

这里记一个 Linux 命令：`getent hosts db`。它按容器内的系统配置查询主机名，结果可能来自 DNS，也可能来自 `/etc/hosts`。用法里的子命令是复数 `hosts`。先确认镜像里有 `getent`，再根据查询结果判断。

## 3. 根据结果继续缩小范围

在这次实验的临时容器里，`getent hosts db` 返回了 IP，而查询不存在的名称没有返回记录。

- 如果查不到 `db`，先用 `docker compose ps -a` 确认数据库容器的状态，再核对 JDBC URL 中的主机名是否与 Compose 服务名一致，以及 `app` 和 `db` 是否共享一个网络。
- 如果能查到地址，接着检查 PostgreSQL 的 `5432` 端口和认证。如果原来的 Java 进程仍报 `UnknownHostException`，也要核对它实际使用的主机名。临时容器里的查询结果只代表当时的系统解析情况。

这个实验里应使用服务名 `db`。容器重建后 IP 可能变化，修正 JDBC URL 时继续使用服务名即可。
