# Docker 故障排查实操（五）：容器因内存不足退出

看到容器突然变成 `Exited (137)`，应用日志又没有明显异常时，我会优先查 OOM（Out of Memory）和容器的内存上限。进程被内核强制终止时，可能来不及写出应用日志。

## 1. 确认 Docker 是否记录了 OOM

```bash
docker ps -a --filter 'exited=137'
docker logs --tail=100 <container>
docker inspect <container> | jq '.[0].State | {Status, ExitCode, OOMKilled, Error}'
```

`137` 通常对应 `SIGKILL`。内核在内存不足时会用它终止进程，手动强制终止也可能得到同样的退出码。判断时我会结合 `OOMKilled`。为 `true` 时，说明 Docker 记录到了这次 OOM 终止。为 `false` 时，先查其他强制终止的线索。`Error` 是 Docker 记录的状态错误文本，OOM 时它也可能为空。

用一个内存上限为 `32 MiB` 的临时容器复现时，我看到 `ExitCode: 137`、`OOMKilled: true`，同时 `Error` 为空。

## 2. 检查实际生效的内存上限

```bash
docker inspect <container> | jq '.[0].HostConfig.Memory'
```

这里的单位是字节。例如 `536870912` 对应 `512 MiB`。返回 `0` 表示 Docker 没有给这个容器单独设置内存上限，此时也要考虑 Docker 所在 Linux 环境的整体内存压力。用 Compose 时，可以把实际值与服务的 `mem_limit` 对照。

如果问题能重现，可以在容器还运行时用 `docker stats <container>` 观察 `MEM USAGE / LIMIT`，看用量是在启动时突然冲高，还是随着请求逐步增长。确认原因后，再决定调整内存上限还是检查应用的用量。
