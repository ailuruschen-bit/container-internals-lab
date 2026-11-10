# Docker `USER`：让应用以非 root 身份运行

[English](../15-docker-user-best-practices.md) | **中文** | [日本語](../ja/15-docker-user-best-practices.md)

今天学习了 Dockerfile 中 `USER` 的安全实践。它解决的问题很直接：如果应用正常运行并不需要管理员权限，就不应该让容器主进程默认以 root 身份运行。

不过，“容器内 root 等于宿主机 root”并不准确。容器仍然受到 Namespace、Capability、Seccomp 和挂载范围等机制限制。使用非 root 用户的意义，是进一步缩小应用被入侵后的权限范围，为容器增加一层纵深防御。

## 1. 不写 `USER` 时会发生什么

如果基础镜像和 Dockerfile 都没有指定其他用户，容器进程通常默认使用 root。

检查前面构建的 Spring Boot Image：

```bash
docker image inspect spring-lab:v2 \
  -f '{{json .Config.User}}'
```

本机结果是空字符串：

```text
""
```

这表示镜像没有显式设置默认用户，运行时会使用 root。也可以直接验证：

```bash
docker run --rm \
  --entrypoint id \
  spring-lab:v2
```

如果输出中出现 `uid=0(root)`，就说明容器进程正在使用 root 身份。

## 2. 容器内 root 不等于瞬间取得宿主机全部权限

即使 UID 是 0，Docker 默认也不会把 Linux root 的所有 Capability 都交给容器，容器看到的文件系统和进程视图也受到隔离。因此，应用漏洞被利用后，攻击者并不会仅凭“容器内是 root”就自动成为宿主机 root。

但容器内 root 仍然拥有更大的破坏能力，例如：

- 读取或修改容器内本来只允许 root 访问的配置和文件。
- 更容易篡改应用进程、启动额外程序或改变文件权限。
- 对挂载进容器的目录造成更大影响。
- 在额外 Capability、特权模式或内核漏洞存在时放大风险。

如果容器还挂载了 Docker Socket、敏感宿主机路径，或者使用 `--privileged` 等高权限配置，风险会进一步上升。

所以使用非 root 不是因为它能代替所有容器隔离，而是遵循最小权限原则：应用只取得完成工作真正需要的权限。

## 3. `USER` 实际影响哪些阶段

基本语法是：

```dockerfile
USER <user>
USER <user>:<group>
USER <UID>:<GID>
```

`USER` 会设置当前 Stage 后续指令使用的默认用户，包括：

- 后续的 `RUN` 构建步骤。
- 容器运行时的 `ENTRYPOINT`。
- 容器运行时的 `CMD`。

例如：

```dockerfile
USER app:app

RUN whoami
ENTRYPOINT ["java", "-jar", "app.jar"]
```

这里的 `RUN whoami` 已经在 Build 阶段使用 `app`，最终 Java 进程也默认使用 `app`。

因此，`USER` 不能随意放在 Dockerfile 开头。切换后再执行安装软件、创建系统目录或修改 root 文件，通常会因为权限不足而失败。

## 4. 推荐顺序：先完成特权操作，最后降权

更清晰的 Dockerfile 顺序是：

```text
以 root 开始 Build Stage
    ↓
安装软件、创建用户和组
    ↓
创建目录并设置所有权
    ↓
复制应用文件
    ↓
USER 切换到普通用户
    ↓
ENTRYPOINT / CMD 启动应用
```

不是先切成普通用户、需要权限时再频繁切回 root。能在前面集中完成的特权操作，应尽量一次完成，并让 Dockerfile 最后的运行身份保持为非 root。

确实需要在后续构建步骤临时切换时，可以明确写：

```dockerfile
USER root
RUN some-privileged-build-step
USER app:app
```

但最终 Stage 最后生效的 `USER` 才决定容器的默认运行身份，不能切回 root 后忘记再次降权。

## 5. 在 Java Runtime Stage 中创建用户

对于之前的 Maven Multi-stage Build，用户应该在最终 JRE Stage 中创建：

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build

WORKDIR /build
COPY pom.xml .
RUN mvn dependency:go-offline
COPY src ./src
RUN mvn clean package -DskipTests


FROM eclipse-temurin:21-jre

RUN groupadd --gid 10001 app && \
    useradd --uid 10001 \
            --gid 10001 \
            --no-create-home \
            app

WORKDIR /app

COPY --from=build \
     --chown=10001:10001 \
     /build/target/*.jar \
     app.jar

USER 10001:10001

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
```

这里有几个关键点：

1. `groupadd`、`useradd` 在 root 身份下执行。
2. 用户创建在最终 JRE Stage，而不是只创建在 Maven Builder Stage。
3. `COPY --chown` 在复制 JAR 时直接设置文件所有者。
4. `USER` 位于需要 root 权限的步骤之后。
5. Java 进程最终以 UID/GID `10001:10001` 运行。

不同基础镜像创建用户的命令可能不同。例如 Alpine 常使用 `addgroup`、`adduser`，Debian 或 Ubuntu 系镜像常使用 `groupadd`、`useradd`，不能把命令机械复制到所有 Base Image。

## 6. 为什么显式指定 UID 和 GID

如果只让系统自动分配 ID，不同构建结果中的 UID/GID 可能不同。容器只访问镜像内部文件时影响不明显，但挂载 Volume 或宿主机目录后，内核真正判断权限时使用的是数字 UID/GID。

显式使用例如：

```text
UID 10001
GID 10001
```

可以让文件所有权更稳定，也更容易与部署环境的存储权限协调。

具体数字并没有统一规定，但应选择基础镜像中尚未被占用的 UID/GID，并在团队和部署环境中保持约定一致。

`USER app:app` 更容易阅读，`USER 10001:10001` 则不依赖运行命令时再次通过名称解析用户。两种方式都可以，关键是镜像中的用户、组和文件权限保持一致。

## 7. `user:group` 的含义

```dockerfile
USER app:app
```

冒号前面是用户，后面是默认组。也可以使用数字：

```dockerfile
USER 10001:10001
```

显式指定 Group 时，进程会使用这个 Group，而且 Dockerfile `USER` 不会自动保留该用户配置的其他附加组。如果应用确实需要额外组权限，应在运行配置中明确处理，而不是假定 `/etc/group` 中列出的所有附加组都会自动生效。

## 8. 非 root 用户必须真正拥有所需权限

只添加一条 `USER app` 并不能自动修复文件权限。应用需要读取 JAR、写日志或创建临时文件时，对应目录必须允许该用户访问。

例如应用需要写入 `/app/data`：

```dockerfile
RUN mkdir -p /app/data && \
    chown -R 10001:10001 /app/data

USER 10001:10001
```

挂载 Volume 或 Bind Mount 后，挂载内容会覆盖镜像中该路径原来的文件视图，其 UID/GID 也来自实际存储。即使 Dockerfile 内已经 `chown`，运行时挂载的目录仍可能因所有权不匹配而出现 `Permission denied`。

因此，切换非 root 后的常见问题往往不是程序本身，而是：

- 应用目录仍属于 root。
- 日志、缓存或上传目录不可写。
- 挂载存储的 UID/GID 与容器用户不匹配。
- 程序依赖了只有 root 才能执行的操作。

正确做法是只授予应用需要的目录权限，而不是为了绕过错误又把整个容器恢复为 root。

## 9. 运行时仍然可以覆盖 `USER`

Dockerfile 中的 `USER` 是安全默认值，不是无法覆盖的强制策略。

`docker run` 可以指定：

```bash
docker run --rm \
  --user 10001:10001 \
  my-app:v1
```

Compose 中对应：

```yaml
services:
  app:
    image: my-app:v1
    user: "10001:10001"
```

运行者也可能显式使用 `--user 0` 覆盖镜像默认值。因此 Dockerfile `USER` 的价值，是让安全用法成为默认行为；更严格的限制还需要部署平台策略、Capability 控制、只读文件系统和避免危险挂载等机制配合。

## 10. 当前形成的理解

我现在对 `USER` 的认识可以概括为：

```text
root 只负责准备运行环境
    ├── 安装依赖
    ├── 创建用户和组
    ├── 创建目录
    └── 配置文件权限
            │
            ▼
USER app:app
            │
            ▼
普通用户负责运行应用
```

使用非 root 用户不能保证容器绝对安全，也不能把容器内 root 简单等同于宿主机 root。它真正解决的是：当应用本身遭到利用时，攻击者首先得到的只是一个权限受限的进程身份，而不是容器内能够访问所有文件和执行所有允许特权操作的 root。

最实用的原则是：**如果应用不需要 root，就在最终运行 Stage 中显式创建用户、准备好所需目录权限，并把 `USER` 作为启动应用前的最后几项配置之一。**
