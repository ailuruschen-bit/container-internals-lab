# Docker Image 构建实操（一）：Hello World

上一篇学习了怎样查看和删除本地镜像，今天第一次用 Dockerfile 构建自己的 Image。

这次程序只有两行 Shell，但实验的重点并不是程序本身，而是观察 Dockerfile 怎样变成 Image，以及 Image 又怎样变成正在运行的 Container。

## 1. 准备最小项目

实验环境仍然是 macOS + OrbStack，基础镜像使用 `alpine:latest`。

项目中只有两个文件：

```text
docker-image-lab/
└── 01-hello/
    ├── Dockerfile
    └── hello.sh
```

`hello.sh`：

```sh
#!/bin/sh
echo "Hello from my first Docker Image!"
pwd
```

`Dockerfile`：

```dockerfile
FROM alpine:latest

WORKDIR /app

COPY hello.sh .

CMD ["sh", "hello.sh"]
```

这个 Dockerfile 最终产生两类构建结果：一类是容器需要看到的文件，另一类是容器启动时使用的默认配置。

## 2. 四条 Dockerfile 指令分别做了什么

### `FROM`：选择构建起点

```dockerfile
FROM alpine:latest
```

`FROM` 指定新镜像基于哪个基础镜像构建。这里会复用 Alpine 已有的文件系统层和相关配置；本地没有该镜像时，构建器可以从镜像仓库拉取。

构建 `hello-lab` 不会修改原来的 Alpine 镜像。新的内容会形成属于新镜像的构建结果。

### `WORKDIR`：设置默认工作目录

```dockerfile
WORKDIR /app
```

`WORKDIR` 会影响后续相关指令使用的工作目录，也会成为容器启动时的默认工作目录。如果 `/app` 不存在，构建过程中会创建它，因此不需要先写：

```dockerfile
RUN mkdir -p /app
```

### `COPY`：把文件放进镜像

```dockerfile
COPY hello.sh .
```

`COPY` 的基本结构是：

```dockerfile
COPY <源路径> <目标路径>
```

源路径来自 Build Context，目标路径属于正在构建的镜像文件系统。这里的目标 `.` 是相对路径，会以此前设置的 `WORKDIR /app` 为基准，所以结果是：

```text
/app/hello.sh
```

这个文件的内容已经进入镜像，不会在容器运行时继续引用 macOS 上原来的 `hello.sh`。因此 `COPY` 与 Bind Mount 完全不同：

- `COPY` 在构建时把文件内容纳入镜像。
- Bind Mount 在运行时把外部路径挂载进容器。

### `CMD`：记录默认启动命令

```dockerfile
CMD ["sh", "hello.sh"]
```

`CMD` 不会在构建镜像时执行 `hello.sh`。它会把默认命令写入 Image Config，之后创建容器时再使用：

```text
docker build  ──→  保存 Cmd 配置
docker run    ──→  使用 Cmd 启动进程
```

这里明确调用了 `sh hello.sh`，所以 Shell 会读取脚本内容，不要求 `hello.sh` 本身具有可执行权限。如果改成直接执行脚本，则还需要正确的可执行权限与解释器设置。

## 3. Exec Form 与 Shell Form

Dockerfile 中的命令存在两种常见形式。

Exec Form 使用 JSON 数组：

```dockerfile
CMD ["sh", "hello.sh"]
```

数组中的第一项是可执行程序，后面是传给它的参数。这种形式默认不会再用一个 Shell 解释整条命令，也不会自动进行 Shell 变量展开，更适合作为容器主进程的启动方式。

Shell Form 是一个命令字符串：

```dockerfile
CMD sh hello.sh
```

在 Linux 容器中，它通常会通过类似下面的方式执行：

```text
/bin/sh -c "sh hello.sh"
```

Shell Form 可以使用 Shell 的变量展开、管道等语法，但额外的 Shell 进程也可能影响信号的接收与转发。

我目前形成的简单区分是：

```text
Exec Form   明确给出程序和参数
Shell Form  先交给 Shell 解释命令字符串
```

## 4. Build Context 是什么

进入项目目录并构建镜像：

```bash
cd "$HOME/docker-image-lab/01-hello"
docker build -t hello-lab:v1 .
```

命令最后的 `.` 表示当前目录是这次构建的 Build Context：

```text
docker build -t hello-lab:v1 .
                                 ↑
                          Build Context
```

构建器只能从 Build Context 中读取普通 `COPY`、`ADD` 等指令需要的本地文件。`COPY hello.sh .` 能找到源文件，就是因为 `hello.sh` 位于当前 Context 中。

Build Context 不等于镜像文件系统，也不是 Context 中的所有文件都会自动出现在最终镜像里。文件要经过相应构建指令处理，才会成为构建结果的一部分。

实际项目还应避免把日志、构建产物、版本库目录或敏感文件放进不必要的构建输入中，可以使用 `.dockerignore` 排除它们。本次目录只有两个文件，所以暂时不需要复杂配置。

构建命令中其他部分的含义是：

- `build`：根据 Dockerfile 执行构建。
- `-t hello-lab:v1`：为结果设置镜像名称和 Tag。
- `.`：指定 Build Context，也让构建器默认查找当前目录下的 `Dockerfile`。

这里还有一个容易混淆的地方：`docker build` 的 `-t` 是 `--tag`，而 `docker run` 的 `-t` 是 `--tty`。同一个短参数在不同子命令中可能有不同含义。

## 5. 从 Build 到 Run

整个过程可以表示为：

```text
Dockerfile + hello.sh
          │
          ▼
     docker build
          │
          ▼
 Image: hello-lab:v1
          │
          ▼
      docker run
          │
          ▼
  Container Process
```

Build 阶段负责生成文件系统内容，并保存镜像的运行配置。Run 阶段则基于镜像创建容器的可写层、网络和进程环境，最后执行启动命令。

使用镜像默认的 `CMD`：

```bash
docker run --rm hello-lab:v1
```

预期输出：

```text
Hello from my first Docker Image!
/app
```

这同时验证了三件事：

- `hello.sh` 已经被复制进镜像。
- `CMD` 在容器启动时得到了执行。
- `WORKDIR` 设置的 `/app` 成为默认工作目录。

`--rm` 表示容器停止后自动删除这个容器，不会删除用来创建它的 Image。

## 6. 覆盖镜像的默认 `CMD`

在镜像名称后指定其他命令：

```bash
docker run --rm hello-lab:v1 echo "CMD overridden"
```

输出变成：

```text
CMD overridden
```

`docker run` 的基本结构是：

```text
docker run [OPTIONS] IMAGE [COMMAND] [ARG...]
```

本次镜像没有设置 `ENTRYPOINT`，因此镜像名称后面的 `COMMAND` 会替换原来的 `CMD`。

同样，也可以把默认命令替换为交互式 Shell：

```bash
docker run --rm -it hello-lab:v1 sh
```

进入后手动检查镜像中的内容：

```sh
pwd
ls -l /app
cat /app/hello.sh
sh /app/hello.sh
```

这时不会自动执行 `hello.sh`，因为 `sh` 已经替换了默认 `CMD`。将来学习 `ENTRYPOINT` 后，还需要重新理解它与 `CMD`、运行命令之间的组合关系。

## 7. Tag 只是镜像的引用名称

本次构建得到的镜像引用是：

```text
hello-lab:v1
```

它可以简单拆成：

```text
repository : tag
hello-lab  : v1
```

如果省略 Tag，Docker 默认尝试使用 `latest`：

```bash
docker run hello-lab
```

这实际会寻找 `hello-lab:latest`。当前只有 `hello-lab:v1`，所以不能因为本地存在 `v1` 就自动找到它。

可以给现有镜像再添加一个标签：

```bash
docker image tag hello-lab:v1 hello-lab:latest
docker image ls
```

之后下面的命令就能找到 `latest`：

```bash
docker run --rm hello-lab
```

这一步不会重新构建镜像，也不会复制一整份镜像文件系统，只是增加了另一个引用名称。同一个 Image 可以有多个 Tag，而 `latest` 也只是普通标签，不保证它一定指向最近构建的内容。

## 8. 检查构建结果

现在可以把上一篇学到的 `inspect` 用在自己构建的镜像上：

```bash
docker image inspect hello-lab:v1
```

只查看 Image Config：

```bash
docker image inspect hello-lab:v1 \
  -f '{{json .Config}}'
```

分别查看工作目录和默认命令：

```bash
docker image inspect hello-lab:v1 \
  -f '{{.Config.WorkingDir}}'

docker image inspect hello-lab:v1 \
  -f '{{json .Config.Cmd}}'
```

这次 Dockerfile 指令与镜像结果的对应关系是：

| Dockerfile 指令 | 构建结果 |
|---|---|
| `FROM` | 以基础镜像的文件系统层和配置为起点 |
| `WORKDIR` | 设置默认工作目录，必要时创建目录 |
| `COPY` | 把 `hello.sh` 加入镜像文件系统 |
| `CMD` | 把默认启动命令写进 Image Config |

因此，这个镜像可以先简单理解成：

```text
Docker Image
   ├── Filesystem Layers
   │      └── /app/hello.sh
   └── Image Config
          ├── WorkingDir: /app
          └── Cmd: ["sh", "hello.sh"]
```

## 9. 今天形成的理解

Dockerfile 是镜像的构建说明，Image 是构建完成的结果，Container 则是基于 Image 创建出来的运行实例：

```text
Dockerfile
    │ docker build
    ▼
Docker Image
    ├── 文件系统内容
    └── 默认运行配置
    │ docker run
    ▼
Container
    ├── 基于镜像创建运行环境
    ├── 增加自己的可写层
    └── 启动实际进程
```

一个 Image 可以重复创建多个 Container，Container 也可以在启动时覆盖镜像中的部分默认配置。

构建成功后，即使原来的 Dockerfile 和 `hello.sh` 不再保留，本地已经生成的镜像仍然能够运行。因为容器真正依赖的是构建产物，而不是再次读取原始项目目录。

最后一个重要认识是：Dockerfile 不能简单理解为一份从上到下执行的 Shell 脚本。不同指令产生的结果并不相同：

```text
COPY  在构建时处理文件内容
CMD   在构建时记录默认运行配置
RUN   在构建时执行命令（留到后续实验）
```

今天完成的是最小的 Image 构建闭环。下一步准备使用 Dockerfile 构建一个自定义 Nginx Web Image，继续观察 `RUN`、构建缓存以及静态资源怎样进入镜像。
