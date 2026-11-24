# Docker 故障排查实操（一）：容器启动后立即停止

这次开始整理常见的 Docker 故障排查场景。第一个现象是：执行 `docker run` 或 `docker compose up` 后，Container 刚刚启动就停止了。

排查时先不急着修改 Dockerfile 或重建 Image，而是按照“状态、日志、详细信息”的顺序收集证据。

为了稳定复现这个现象，本次使用一个输出错误后返回 `1` 的临时 Container：

```bash
docker run --name note-exit-lab \
  alpine sh -c 'echo "application startup failed" >&2; exit 1'
```

这里没有使用 `--rm`，因为退出后的 Container 必须保留下来，后续才能检查它的状态和详细信息。实验结束后再执行 `docker rm note-exit-lab` 清理。

## 1. 先确认 Container 的实际状态

`docker ps` 默认只显示正在运行的 Container。已经退出的实例需要使用：

```bash
docker ps -a
```

实验中可以看到：

```text
CONTAINER ID   IMAGE    STATUS       NAMES
b70b6baf23b2   alpine   Exited (1)   note-exit-lab
```

这里先得到三个有用信息：

- Container ID 或名称，供后续命令使用。
- 当前状态确实是 `Exited`，而不是不断重启或仍在运行。
- 括号中的 `1` 是主进程最后一次退出时的 Exit Code。

Container 停止不一定代表 Docker 本身发生故障。Container 的生命周期与主进程绑定，主进程正常执行完毕也会停止，此时常见的 Exit Code 是 `0`。因此真正需要判断的是：它为什么退出，以及这个退出是否符合预期。

## 2. 查看主进程留下的日志

拿到 ID 或名称后，下一步查看日志：

```bash
docker logs <container>
```

例如：

```bash
docker logs b70b6baf23b2
```

本次实验得到：

```text
application startup failed
```

日志较多时，可以先看最后一部分：

```bash
docker logs --tail 100 <container>
```

`docker logs` 主要取得 Container 写到标准输出和标准错误中的内容。如果应用把日志只写入内部文件，或者在来得及输出日志之前就失败，这里也可能是空的。因此，“没有日志”不等于“没有错误”。

## 3. 先看完整 `inspect`，再逐步缩小字段

`inspect` 的字段很多，不需要一开始就记住所有路径。更实用的方法是先用 `docker ps -a` 取得 Container ID，再查看完整 JSON：

```bash
docker inspect <container>
```

输出最外层是数组，内部包含 Container 的配置、运行状态、挂载和网络等详细信息。先从完整结构中确认字段位置，再使用 `-f` 提取需要的部分。

当前场景最值得先看的对象是 `.State`：

```bash
docker inspect -f '{{json .State}}' <container>
```

实验输出类似：

```json
{"Status":"exited","Running":false,"OOMKilled":false,"ExitCode":1,"Error":"","StartedAt":"...","FinishedAt":"..."}
```

`json` 会把 `.State` 这个嵌套对象输出成合法 JSON，比模板的默认格式更容易阅读。

## 4. `.State` 中重点关注什么

| 字段 | 作用 |
|---|---|
| `Status` | 当前处于 `running`、`exited`、`restarting` 等哪种状态 |
| `ExitCode` | 主进程最后一次退出时返回的状态码 |
| `Error` | Docker Runtime 在启动 Container 时记录的错误 |
| `OOMKilled` | 进程是否因为内存不足而被 OOM 机制终止 |
| `Restarting` | Container 是否正在按照重启策略反复重启 |
| `StartedAt` | 最近一次启动时间 |
| `FinishedAt` | 最近一次停止时间 |

如果只想继续缩小到一个字段，也可以直接访问：

```bash
docker inspect -f '{{.State.ExitCode}}' <container>
docker inspect -f '{{.State.OOMKilled}}' <container>
docker inspect -f '{{.State.Error}}' <container>
```

## 5. Exit Code 只能提供方向

几个常见值可以帮助缩小范围：

| Exit Code | 常见含义 |
|---|---|
| `0` | 主进程正常结束，不一定是故障 |
| `1` | 应用返回的一般错误，需要结合日志判断 |
| `126` | 找到了命令，但无法执行，例如权限问题 |
| `127` | 找不到需要执行的命令 |
| `137` | 进程收到 `SIGKILL`，可能是 OOM，也可能是人为强制终止 |
| `143` | 进程收到 `SIGTERM`，常见于正常停止过程 |

Exit Code 不是完整诊断。例如看到 `137` 时，不能直接断定内存不足，还需要检查：

```bash
docker inspect -f '{{.State.OOMKilled}}' <container>
```

## 6. 状态仍不足以定位时

如果日志和 `.State` 只告诉我“启动失败”，却没有说明为什么，可以继续检查 Container 实际使用的启动配置：

```bash
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

这可以确认实际入口程序和默认参数，排查命令写错、文件不存在或启动参数不符合预期等问题。

## 7. 当前形成的排查顺序

```text
docker ps -a
    │
    ├── 确认 Container 状态、ID 和 Exit Code
    ▼
docker logs <container>
    │
    ├── 查看应用写入标准输出和标准错误的内容
    ▼
docker inspect <container>
    │
    ├── 先观察完整 JSON
    ▼
docker inspect -f '{{json .State}}' <container>
    │
    └── 根据结果继续查看 ExitCode、OOMKilled、Error 或启动配置
```

这套流程的重点不是背诵所有 `inspect` 字段，而是先用完整 JSON 建立地图，再通过 `-f` 逐步缩小范围。对于“启动后立即停止”，`.State` 是最合适的第一个切入点。
