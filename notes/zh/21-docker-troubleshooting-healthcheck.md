# Docker 故障排查实操（四）：健康检查为什么失败？

`docker ps` 里看到 `Up (unhealthy)`，说明容器还在运行，健康检查已经失败。要知道检查命令报了什么错，我会用容器名或 ID 去查 `inspect` 的 `.State.Health`。

## 1. 用 inspect 找到健康检查记录

沿用之前的 `-f` 写法，可以直接取出健康状态对象：

```bash
docker inspect -f '{{json .State.Health}}' <container>
```

想看有缩进的 JSON，可以把完整的 `inspect` 输出交给本机的 `jq`：

```bash
docker inspect <container> | jq '.[0].State.Health'
```

完整输出的最外层是数组，`.[0]` 取出第一个容器对象，再沿着 `.State.Health` 找到健康检查记录。如果结果是 `null`，可以先确认容器是否启用了健康检查。

## 2. 先看最近一次检查

```bash
docker inspect <container> | jq '.[0].State.Health.Log[-1]'
```

`[-1]` 取数组最后一条记录。我会先看 `ExitCode` 和 `Output`，再看 `Start`、`End` 核对失败时间。上一级的 `Status` 是当前健康状态，`FailingStreak` 是连续失败次数。

例如，我用临时容器检查一个尚未创建的文件。容器仍在运行，最近一次检查的 `ExitCode` 是 `1`，`Output` 显示 `ready file missing`。这里看到的是检查命令写出的内容，查应用本身的输出时再用 `docker logs`。

如果输出还不足以定位，可以用下面的命令确认实际生效的检查配置，再到原容器里手动运行同一条检查命令，核对报错和退出码。

```bash
docker inspect <container> | jq '.[0].Config.Healthcheck'
```
