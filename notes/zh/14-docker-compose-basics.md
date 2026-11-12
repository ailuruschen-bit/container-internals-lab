# Docker Compose 初步：从 `docker run` 到多服务配置

[English](../14-docker-compose-basics.md) | **中文** | [日本語](../ja/14-docker-compose-basics.md)

今天开始学习 Docker Compose，目前只完成了最基础的配置实验。这篇先记录我对 `compose.yaml`、Service、端口与环境变量，以及服务启动依赖和健康检查的理解。

Compose 最直接的价值，是把原本分散在多条 `docker run` 命令中的容器配置，集中保存为一份可以重复执行的 YAML 文件。

## 1. `compose.yaml` 描述的是什么

单独运行容器时，我可能会写：

```bash
docker run -d \
  --name spring-app \
  -p 127.0.0.1:8081:8080 \
  -e SPRING_PROFILES_ACTIVE=dev \
  spring-lab:v2
```

使用 Compose 后，可以把相同信息写成：

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: dev
```

对应关系是：

| `docker run` | Compose Service 配置 |
|---|---|
| `spring-lab:v2` | `image: spring-lab:v2` |
| `--name` | 通常由 Compose 根据项目名和 Service 名管理 |
| `-p`、`--publish` | `ports` |
| `-e`、`--env` | `environment` |

Compose 文件描述的是希望一组服务以什么配置运行。执行：

```bash
docker compose up -d
```

Compose 会读取当前目录的 `compose.yaml`，创建所需网络和容器，并在后台启动服务。

## 2. Service 与 Container 的关系

`services` 下的每一个名称代表一个服务定义：

```yaml
services:
  app:
    image: spring-lab:v2

  cache:
    image: redis:7-alpine
```

这里的 `app` 和 `cache` 是 Service 名，不是 Image 名，也不等于必须手工指定的 Container 名。

我目前可以把它理解为：

```text
Service
    ├── 使用哪个 Image 或怎样 Build
    ├── 怎样配置容器运行环境
    └── 与其他 Service 有什么关系
              │
              ▼
       Compose 创建 Container
```

默认情况下，一个 Service 创建一个 Container。Compose 也可以调整副本数量，但 Service 是配置和管理单位，Container 是实际运行实例，二者不能完全画等号。

## 3. Compose 不要求 Image 一定提前 Build

如果写的是：

```yaml
services:
  app:
    image: spring-lab:v2
```

Compose 会使用本地已有 Image；本地没有时，通常会按照拉取策略尝试从 Registry 获取。

但 Compose 也可以直接声明构建方式：

```yaml
services:
  app:
    build:
      context: ../04-java/springboot
      dockerfile: Dockerfile
    image: spring-lab:dev
```

这里的 `build` 告诉 Compose 从源码构建，`image` 则为这次构建结果设置 Image 名称和 Tag。

### `context` 与 `dockerfile`

`context` 对应 `docker build` 最后的 Build Context：

```text
compose.yaml 所在目录
    │
    └── ../04-java/springboot
            ├── Dockerfile
            ├── pom.xml
            └── src/
```

相对 Context 路径从 Compose Project 目录解析。本次的 `../04-java/springboot` 表示 Spring Boot 项目目录，Dockerfile 中的 `COPY` 也只能使用该 Context 允许访问的文件。

`dockerfile` 用来指定构建文件，它的相对路径以 Build Context 为基准。如果文件名就是 Context 根目录中的标准 `Dockerfile`，可以省略这一项；`context` 没有显式配置时也默认使用项目目录 `.`。

因此，这两项不是任何情况下都必须同时写出。使用完整写法的好处，是构建输入和 Dockerfile 位置更加明确。

### `image` 给构建结果命名

`build` 与 `image` 是同一个 Service 下的两个配置：

```yaml
build:
  context: ../04-java/springboot
  dockerfile: Dockerfile
image: spring-lab:dev
```

当 Compose 执行这次 Build 时，生成的 Image 会得到：

```text
spring-lab:dev
```

如果没有写 `image`，Compose 仍然能构建，并通常使用由 Project 与 Service 组合出的名称；但这不是自己明确约定的发布名称，执行 Compose Push 时也无法据此确定要推送到哪个 Registry Repository。因此，需要分发构建结果时应显式配置 `image`。

如果 Dockerfile 就位于 Build Context 根目录，并且文件名就是默认的 `Dockerfile`，`build` 还可以使用短写法：

```yaml
services:
  app:
    build: ../04-java/springboot
    image: spring-lab:dev
```

这里 `build` 后面的路径直接表示 Build Context，Compose 会在该目录中寻找默认的 `Dockerfile`。它相当于只配置 `context` 而省略 `dockerfile`；如果 Dockerfile 位于其他位置或使用了其他文件名，仍然需要使用前面的完整写法。

可以单独构建：

```bash
docker compose build app
```

也可以在启动前要求 Compose 执行构建：

```bash
docker compose up --build -d
```

当 `build` 和 `image` 同时存在时，是否优先尝试 Pull 还会受到 `pull_policy` 和具体命令影响。当前实验通过 `--build` 明确要求启动前构建，避免把“写了 build 就一定每次自动重建”当成固定规则。

因此，“Compose 必须使用已经 Build 好的 Image”并不准确。更完整的说法是：

> 每个 Service 最终都需要一个 Image，但这个 Image 可以事先存在、从 Registry 拉取，也可以由 Compose 根据 `build` 配置生成。

## 4. `ports`：对应 `docker run -p`

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
```

三个数字部分的含义仍然和 `docker run -p` 相同：

```text
宿主机地址 : 宿主机端口 : 容器端口
127.0.0.1 :    8081    :   8080
```

`ports` 是列表，因此同一个 Service 可以声明多个端口映射。将短语法写成字符串并加引号，可以避免 YAML 把某些包含冒号或特殊格式的值解析成其他类型。

端口发布主要用于从 Compose 网络外部访问服务。同一个 Compose 项目中的 Service 互相通信时，通常直接使用对方的 Service 名和容器端口，不需要绕到宿主机发布端口。

## 5. `environment`：对应 `docker run -e`

Map 写法：

```yaml
services:
  app:
    image: spring-lab:v2
    environment:
      SPRING_PROFILES_ACTIVE: dev
      LOG_LEVEL: INFO
```

也可以使用列表写法：

```yaml
environment:
  - SPRING_PROFILES_ACTIVE=dev
  - LOG_LEVEL=INFO
```

这些变量会进入 Compose 创建的 Container，并覆盖 Image 中同名的默认 `ENV`，不会回头修改 Image。

密码、令牌等敏感信息不适合直接提交在 Compose 文件中。可以先通过 `.env` 或 `env_file` 将具体值与 Compose 文件分开，但它们仍然是本地明文文件，并不等同于专门的 Secret 管理机制。

## 6. `volumes`：声明存储并挂载到 Service

这次 PostgreSQL Service 增加了命名 Volume：

```yaml
services:
  db:
    image: postgres:17-alpine
    volumes:
      - pgdata:/var/lib/postgresql/data

volumes:
  pgdata:
```

这里出现了两处 `volumes`，职责并不相同。

### 顶级 `volumes` 声明命名 Volume

```yaml
volumes:
  pgdata:
```

它告诉 Compose，当前 Project 中有一个逻辑名称为 `pgdata` 的命名 Volume。空配置表示使用 Docker Engine 默认的 Volume Driver。

仅仅在文件中声明并不会马上创建实际资源。执行 `docker compose up` 时，Compose 会在需要时创建；已经存在时则继续复用。

Compose 默认会把 Project 名加入实际资源名称。本次可以通过：

```bash
docker volume ls
```

观察到：

```text
05-compose_pgdata
```

`pgdata` 是 Compose 文件中的逻辑名，`05-compose_pgdata` 是 Docker Engine 中实际可见的 Volume 名。需要时还可以执行：

```bash
docker volume inspect 05-compose_pgdata
```

### Service 内的 `volumes` 指定挂载关系

```yaml
services:
  db:
    volumes:
      - pgdata:/var/lib/postgresql/data
```

短语法用冒号分成：

```text
Volume 逻辑名 : Container 内挂载路径
pgdata        : /var/lib/postgresql/data
```

顶级声明只定义可用的命名 Volume，并不会自动授予任何 Service 使用。Service 内的配置才把 `pgdata` 挂载到 PostgreSQL 实际保存数据库文件的位置。

### 为什么 Container 删除后数据仍然存在

数据库文件不再只写入 `db` Container 的可写层，而是写入独立 Volume：

```text
05-compose-db-1
    │ /var/lib/postgresql/data
    ▼
05-compose_pgdata
```

执行 `docker compose down` 后，Container 和默认 Network 被删除，但命名 Volume 默认保留。本次实验中 Compose Container 已经停止并删除，`05-compose_pgdata` 仍然可以通过 `docker volume ls` 观察到。

下次 `docker compose up` 会重新创建 `db` Container，并挂载原来的 Volume，因此数据库能够继续读取此前写入的数据。

这就是数据持久化的基本含义：数据生命周期不再与某一个 Container 完全绑定。

不过，Volume 持久化不等于已经完成数据库备份、跨主机复制或版本升级。执行下面的命令还会主动删除 Compose 管理的 Volume：

```bash
docker compose down -v
```

其中可能包含数据库数据，因此不能把 `-v` 当作普通清理参数随意使用。

## 7. Compose 默认建立服务网络

包含多个 Service 时，Compose 默认会为当前项目创建一个网络，并把未指定其他网络的服务接入其中。

同一网络中的 Container 可以通过 Service 名解析对方：

```text
app  ──→  cache:6379
```

这里应该使用 `cache` 这个稳定的 Service 名，而不是记住某次创建时动态分配的 Container IP。

这也是 Compose 不只是“把多条 run 命令写进 YAML”的地方：它还以项目为单位管理容器之间的网络和生命周期。

### Compose 创建的仍然是 Docker 资源

Compose 提供了 Service 和 Project 这一层管理抽象，但最终资源仍然由同一个 Docker Engine 创建和管理，因此可以执行：

```bash
docker ps
```

观察到 Service 对应的实际 Container：

```text
05-compose-app-1
05-compose-db-1
```

`docker compose ps` 只查看当前 Compose Project，`docker ps` 则查看当前 Docker Engine 中所有正在运行的 Container。

默认网络也能通过普通 Docker 命令观察：

```text
docker network ls
    └── 05-compose_default
```

其中 `05-compose` 是 Project 名，`default` 是 Compose 自动创建的默认 Bridge Network。需要查看连接详情时，可以使用 `docker network inspect 05-compose_default`。

```text
Compose Service 定义  ──→  Docker Container
Compose 默认网络      ──→  Docker Bridge Network
```

Service 与 Container 仍不能完全画等号，因为一个 Service 可以拥有多个副本；每个实际副本才是 Docker Engine 中可见的 Container。

## 8. `depends_on` 只写服务名时保证什么

短写法：

```yaml
services:
  app:
    image: spring-lab:v2
    depends_on:
      - cache

  cache:
    image: redis:7-alpine
```

Compose 会先创建和启动 `cache`，再启动 `app`，关闭时则按照相反的依赖顺序处理。

但“Container 已经启动”不等于“里面的应用已经准备好接收请求”。例如数据库进程可能已经运行，却仍在执行初始化，短写法的 `depends_on` 不会等待它真正可用。

因此，`depends_on` 解决的是依赖顺序，不会自动理解一个应用的业务就绪状态。

## 9. `healthcheck`：定义怎样才算健康

正确的配置名是 `healthcheck`，不是 `health_check`：

```yaml
services:
  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

这里表示 Docker 会定期在 `cache` Container 内执行：

```text
redis-cli ping
```

命令成功时，Container 可以进入 `healthy` 状态。各字段的作用是：

- `test`：实际执行的检查命令。
- `interval`：两次检查之间的间隔。
- `timeout`：单次检查允许的最长时间。
- `retries`：连续失败多少次后标记为不健康。
- `start_period`：为应用启动预留时间，这段时间内的失败不立即计入失败次数。

检查命令是在目标 Container 内运行的，因此 Image 中必须存在 `redis-cli`、`curl` 或检查所需的其他程序。宿主机安装了某个命令，不代表容器内也能使用。

健康检查只是产生健康状态，不会因为 Container 变成 `unhealthy` 就自动修复应用。怎样重启、替换或告警，还取决于后续的运行与编排机制。

## 10. 让依赖等待健康状态

要让 `app` 等待 `cache` 真正健康，需要使用 `depends_on` 的长写法：

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: compose
    depends_on:
      cache:
        condition: service_healthy

  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

启动顺序变成：

```text
创建 cache
    ↓
启动 cache
    ↓
healthcheck 通过
    ↓
创建并启动 app
```

这个例子只用于说明 Compose 的依赖与健康机制。当前 `spring-lab:v2` 是否真的使用 Redis，还取决于 Spring Boot 项目自身的依赖和配置；仅在 Compose 中声明 `depends_on` 不会让应用自动获得 Redis 功能。

## 11. 最基础的 Compose 操作

在 `compose.yaml` 所在目录中：

```bash
docker compose config
```

先解析并输出最终配置，适合在启动前发现 YAML 或配置合并问题。

创建并启动服务：

```bash
docker compose up -d
```

查看状态和日志：

```bash
docker compose ps
docker compose logs -f
```

停止并删除当前项目创建的 Container 和默认 Network：

```bash
docker compose down
```

`down` 默认不会删除 Image，也不会删除命名 Volume。`docker compose down -v` 会额外删除 Compose 管理的 Volume，里面可能有数据库等持久化数据，不能把它当成日常无风险清理命令。

## 12. 当前形成的理解

我现在先把 Compose 理解成两层：

```text
Service 构建与运行配置
    ├── build / image
    ├── ports / environment
    └── volumes 挂载

多 Service 关系
    ├── 默认网络与名称解析
    ├── depends_on
    └── healthcheck

独立资源
    ├── Image
    ├── Network
    └── Named Volume
```

`compose.yaml` 并不是把 Shell 命令逐字翻译成 YAML，而是在声明一组服务希望达到的运行状态。Compose 再根据这份配置创建 Image、Network 和 Container，并统一管理它们的启动与停止。

不过，Compose 目前解决的主要是一个 Docker 环境中的多容器应用定义与生命周期管理。它具备一定的编排能力，但不能直接等同于跨多台服务器进行调度、自愈和滚动发布的集群编排系统。

这仍然只是 Compose 学习的开头。环境变量文件和敏感配置的处理方式单独记录在下一篇：[Docker Compose 配置实操：从 `.env` 到 Secrets](16-docker-compose-env-and-secrets.md)。后续还需要继续理解配置覆盖、重建行为，以及完整应用栈的实际组织方式。
