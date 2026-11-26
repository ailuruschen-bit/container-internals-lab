# Docker 故障排查实操（二）：容器还在运行，服务却挂了

[English](../19-docker-troubleshooting-service-down.md) | **中文** | [日本語](../ja/19-docker-troubleshooting-service-down.md)

上一次看到的是容器退出。这次 `docker ps` 里容器仍然是 `Up`，但服务已经无法响应。只要容器的主进程还活着，Docker 就会继续显示 `Up`。接下来要确认的是服务进程是否还在，以及它出问题前留下了什么日志。

## 1. 从状态和最近的日志查起

先用 `docker ps` 找到容器名或 ID。在 `compose.yaml` 所在目录执行 `docker compose ps`，列表只显示当前项目的运行中容器。要看同一项目里已经退出的容器，补上 `-a`。

| 要做的事 | 按容器名或 ID | 按 Compose 服务名 |
|---|---|---|
| 看最近 20 行并跟随新日志 | `docker logs -f --tail=20 <container>` | `docker compose logs -f --tail=20 <service>` |
| 直接看进程 | `docker top <container>` | `docker compose top <service>` |
| 打开交互式 Shell | `docker exec -it <container> sh` | `docker compose exec <service> sh` |

如果服务只有一个容器，两列命令通常能完成同样的排查。比如 `app` 是 Compose 服务名，`05-compose-app-1` 是容器名。服务有多个副本时，Compose 的 `logs` 和 `top` 可以查看多个副本，`exec` 可用 `--index` 选定一个。

我通常先看最后 20 行，再重试一次失败的请求，看看有没有新的报错。按 `Ctrl+C` 可以退出日志跟随，容器会继续运行。

## 2. 确认服务进程是否还在

只想确认进程列表时，先用 `docker top`，它从容器外列出进程。如果还要核对配置文件、权限或启动参数，再用 `docker exec -it <container> sh` 进入容器，并运行 `ps`。这需要镜像里有 `sh` 和 `ps`。

如果只剩启动脚本等进程，预期的服务进程已经不在，就回到日志查它为何退出。服务进程还在时，可以从容器内部请求服务的监听端口，进一步判断是服务自己无法响应，还是只有容器外访问失败。

## 3. 把修复写回项目

进容器可以临时改个配置验证猜测。改动如果只留在当前容器的可写层，容器重建时就会丢失。确认原因后，把修复写回 `compose.yaml`、宿主机上挂载的配置文件或应用源码，具体改哪处取决于故障来源。

例如，修正 `compose.yaml` 中 `app` 的环境变量后，在项目目录执行：

```bash
docker compose up -d app
docker compose ps
docker compose logs --tail=20 app
```

`up` 会根据配置变化重建容器。单独执行 `docker compose restart app` 只会重启现有容器，沿用创建时的环境变量等配置。如果修复需要重新构建镜像，用 `docker compose up -d --build app`。最后再请求一次原本失败的接口，确认服务真的恢复了。
