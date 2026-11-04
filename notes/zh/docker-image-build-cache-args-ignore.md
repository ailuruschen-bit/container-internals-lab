# Docker Image 构建实操（三）：缓存、ARG/ENV 与 .dockerignore

[English](../docker-image-build-cache-args-ignore.md) | **中文** | [日本語](../ja/docker-image-build-cache-args-ignore.md)

前两次实验解决了“怎样构建一个镜像”。这一次继续观察构建过程本身：Docker 为什么能加快重复构建、怎样向构建阶段和运行阶段传递不同的变量，以及怎样控制 Build Context 中有哪些文件。

这三个实验看似独立，实际都在影响同一件事：**哪些输入参与镜像构建，以及输入变化后哪些步骤需要重新执行。**

## 1. 观察 Build Cache

第一个实验使用了类似下面的 Dockerfile：

```dockerfile
FROM alpine:latest

RUN apk add --no-cache curl

WORKDIR /app

COPY message.txt .

CMD ["cat", "message.txt"]
```

构建时增加 `--progress=plain`：

```bash
docker build \
  --progress=plain \
  -t cache-lab:v1 .
```

默认进度界面会动态刷新，`plain` 则把构建步骤以普通文本完整输出，更容易观察每一步是否命中缓存。

第一次构建时，各步骤都需要真正执行。Docker 会保存可以复用的构建结果。Dockerfile 和构建输入都没有变化时再次执行相同命令，可以在输出中看到类似：

```text
CACHED
```

`--progress=plain` 只是改变输出方式，不会主动开启或关闭缓存。Build Cache 原本就是构建器的一部分。

### 缓存为什么会失效

Dockerfile 中的每个构建步骤都会依赖前一步的结果以及当前指令的输入：

```text
FROM
  ↓
RUN apk add curl
  ↓
WORKDIR /app
  ↓
COPY message.txt .
  ↓
CMD ...
```

如果只修改 `message.txt`，`COPY` 的输入发生变化，它不能继续使用原来的结果。位于它之前的 `RUN apk add` 没有受到影响，仍然可以命中缓存；但 `COPY` 之后的步骤依赖新的上层结果，通常也需要基于这条新路径重新计算。

因此，Dockerfile 的顺序会直接影响重复构建的速度。一个实用原则是：

> 相对稳定、执行成本高的步骤放在前面；经常变化的内容放在后面。

例如，应用源码经常变化，就不应该在安装稳定系统依赖之前先 `COPY . .`：

```dockerfile
FROM alpine:latest

RUN apk add --no-cache curl

WORKDIR /app
COPY message.txt .

CMD ["cat", "message.txt"]
```

这样修改 `message.txt` 时，不必重新安装 `curl`。

真实项目中的依赖也可以进一步拆分。例如先复制依赖清单并安装依赖，最后再复制经常变化的源代码。只有依赖清单变化时，昂贵的依赖安装步骤才需要重新执行。

如果要明确放弃已有缓存，可以使用：

```bash
docker build --no-cache -t cache-lab:v1 .
```

但日常构建不应该习惯性禁用缓存。更有价值的做法是安排好 Dockerfile 顺序，让缓存失效范围尽可能符合文件的真实变化范围。

## 2. `ARG`：传递构建阶段参数

第二个实验先在 Dockerfile 中声明构建参数：

```dockerfile
FROM alpine:latest

ARG BUILD_LABEL=experiment

RUN echo "build label: $BUILD_LABEL"
```

`ARG` 的默认值是 `experiment`。直接构建时，后续构建指令可以读取这个值：

```bash
docker build \
  --progress=plain \
  -t lab-env:v1 .
```

也可以在命令行覆盖默认值：

```bash
docker build \
  --build-arg BUILD_LABEL=release \
  --progress=plain \
  -t lab-env:v2 .
```

这里发生的是一次新的构建参数输入。使用了 `BUILD_LABEL` 的步骤可能因此失去原有缓存。

`ARG` 的作用范围是镜像构建过程。它不会因为在 Build 阶段出现过，就自动成为容器运行时的环境变量。

## 3. `ENV`：保存镜像的默认环境变量

同一个 Dockerfile 中还可以加入：

```dockerfile
ENV RUN_MODE=production

CMD ["sh", "-c", "echo RUN_MODE=$RUN_MODE; echo BUILD_LABEL=${BUILD_LABEL:-unset}"]
```

完整关系可以简化为：

```dockerfile
FROM alpine:latest

ARG BUILD_LABEL=experiment
ENV RUN_MODE=production

RUN echo "build label: $BUILD_LABEL"

CMD ["sh", "-c", "echo RUN_MODE=$RUN_MODE; echo BUILD_LABEL=${BUILD_LABEL:-unset}"]
```

运行镜像：

```bash
docker run --rm lab-env:v1
```

得到的结果类似：

```text
RUN_MODE=production
BUILD_LABEL=unset
```

这个结果说明：

- `RUN_MODE` 由 `ENV` 写入 Image Config，创建容器时默认仍然存在。
- `BUILD_LABEL` 只通过 `ARG` 提供给构建阶段，没有自动进入运行环境。

`ENV` 也不只是 Run 阶段才能访问。它在 Dockerfile 中声明以后，后续构建指令同样可以读取；与 `ARG` 的关键区别，是它还会作为镜像的默认环境变量保留下来。

运行容器时可以覆盖默认值：

```bash
docker run --rm \
  -e RUN_MODE=development \
  lab-env:v1
```

这次容器会看到：

```text
RUN_MODE=development
```

`-e` 或 `--env` 改变的是新容器的运行配置，不会回头修改原镜像中的 `RUN_MODE=production`。下次不带 `-e` 创建容器时，仍然会使用镜像默认值。

### `ARG` 与 `ENV` 的关系

| 对比 | `ARG` | `ENV` |
|---|---|---|
| 主要用途 | 构建参数 | 镜像及容器的环境变量 |
| Dockerfile 后续构建步骤可读取 | 可以 | 可以 |
| 默认保存在容器运行环境中 | 不会 | 会 |
| 命令行覆盖方式 | `docker build --build-arg` | `docker run -e` |

如果确实希望把构建参数转成镜像默认环境变量，需要明确写出这种传递关系：

```dockerfile
ARG APP_VERSION
ENV APP_VERSION=$APP_VERSION
```

这样 `APP_VERSION` 才会进入最终 Image Config。

不过，`ARG` 和 `ENV` 都不适合传递密码、令牌或私钥。构建参数可能出现在构建记录或元数据中，`ENV` 更会直接保存在镜像配置里。敏感信息应该使用专门的构建 Secret 或运行时 Secret 机制，而不是依靠变量名称把它隐藏起来。

## 4. `.dockerignore`：先过滤 Build Context

第三个实验使用了：

```dockerfile
FROM alpine:latest

WORKDIR /app

COPY . .

CMD ["ls", "-la", "/app"]
```

`COPY . .` 的范围很大：第一个 `.` 表示 Build Context 中可用的内容，第二个 `.` 表示镜像当前工作目录。

如果项目目录中同时存在源码、日志、临时文件和本地工具目录，直接复制全部内容可能把不需要的文件也放进镜像。

可以在 Build Context 根目录创建 `.dockerignore`：

```text
.git
*.log
tmp/
secret.txt
```

再次构建时，与这些规则匹配的内容会在构建上下文准备阶段被排除，后续 `COPY . .` 无法再复制它们。

它的目的与 `.gitignore` 相似，都是用规则排除不想处理的文件，但两者属于不同工具，具体匹配规则也不能简单认为完全相同。

`.dockerignore` 带来的作用包括：

- 减少发送给构建器的 Context 内容。
- 避免无关文件被 `COPY` 进镜像。
- 减少无关文件变化导致 `COPY` 缓存失效。
- 降低把本地日志、版本库目录或敏感文件误打包的风险。

最后一点尤其重要，但 `.dockerignore` 只是防止误包含的一层保护，不应该被当作完整的 Secret 管理方案。真正的凭据最好从一开始就不要放进普通构建上下文，也不要写入镜像层。

### `.dockerignore` 与缓存的联系

这一步又回到了第一个实验。

假设项目中有一个不断变化的 `debug.log`。如果它属于 `COPY . .` 的输入，那么每次日志变化都可能让这一层失去缓存，即使应用代码完全没有改变。

把 `*.log` 写入 `.dockerignore` 后，日志不再参与这次 `COPY` 的输入，其变化也就不会再无意义地破坏这一步的缓存。

因此，`.dockerignore` 不只是在缩小镜像内容，也是在帮助构建器更准确地判断真正发生变化的输入。

## 5. 今天形成的理解

这三轮实验可以连接成一条完整的构建输入链路：

```text
项目目录
    │
    │ .dockerignore 过滤
    ▼
Build Context
    │
    ├── Dockerfile 指令
    ├── COPY 使用的文件
    └── --build-arg 提供的参数
              │
              ▼
       Build Cache 判断
              │
              ▼
         Docker Image
              │
              ├── ARG 默认不进入运行环境
              └── ENV 保存为默认环境变量
                          │
                          ▼
                    docker run -e
                    创建容器时覆盖
```

Build Cache 不是一个简单的全局开关，而是建立在每一步指令及其输入之上的复用机制。前面某一步的结果变化，会改变后续步骤依赖的基础。

`ARG` 与 `ENV` 也不是同一种变量的两种写法：`ARG` 主要解决怎样参数化构建，`ENV` 则会参与构建并继续成为容器的默认运行环境。

`.dockerignore` 位于更靠前的位置，它先决定哪些本地文件有资格进入 Build Context，从而同时影响 `COPY` 的内容、构建效率和缓存稳定性。

这次实验让我开始把 Dockerfile 看成一个有输入、有依赖关系并且可以复用中间结果的构建过程，而不再只是几条按顺序排列的命令。
