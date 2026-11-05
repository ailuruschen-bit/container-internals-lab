# Docker Image 构建实操（四）：Java 与 Multi-stage Build（进行中）

今天开始构建 Java 应用镜像。实验还没有全部结束，这篇先记录目前完成的两部分：先在一个镜像中完成编译与运行，再使用 Multi-stage Build 把编译环境和运行环境分开。

这次最直观的变化是，Dockerfile 不再只是复制一个现成程序，而是开始承担真正的构建工作。

## 1. 单阶段构建：在镜像中编译 Java

第一个版本使用带有 JDK 的基础镜像，在同一个构建阶段中完成源码编译、JAR 打包和程序运行。

Dockerfile 可以简化为：

```dockerfile
FROM eclipse-temurin:21-jdk

WORKDIR /app

COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class

CMD ["java", "-jar", "app.jar"]
```

这里第一次实际使用了 `RUN`。

### `RUN` 在 Build 阶段执行

```dockerfile
RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class
```

构建器会在构建过程中执行 `javac` 和 `jar`：

```text
Hello.java
    │ javac
    ▼
Hello.class
    │ jar
    ▼
app.jar
```

命令执行完成后，生成的 `Hello.class` 和 `app.jar` 会成为该构建阶段文件系统结果的一部分。

这与之前学到的 `CMD` 不同：

```text
RUN  ──→ docker build 时执行，用来生成镜像内容
CMD  ──→ docker run 时执行，用来启动容器进程
```

因此，构建镜像时 Java 程序不会通过 `CMD` 启动；只有以后创建容器时，才会执行：

```text
java -jar app.jar
```

### 构建并运行

```bash
docker build -t java-lab:v1 .
docker run --rm java-lab:v1
```

这个版本已经能够正常运行，但最终 Image 仍然以完整 JDK 镜像为基础。JDK 中包含编译器和其他开发工具，而容器运行已经编译好的 JAR 时，通常并不需要继续保留这些构建工具。

本机实验中，`java-lab:v1` 的大小约为：

```text
746 MB
```

这引出了下一个问题：能不能在构建时使用 JDK，最终镜像却只保留运行 Java 程序需要的环境？

## 2. Multi-stage Build：分开编译与运行

Multi-stage Build 允许一个 Dockerfile 中出现多个 `FROM`。每个 `FROM` 都会开始一个新的构建阶段。

本次实验可以写成：

```dockerfile
FROM eclipse-temurin:21-jdk AS builder

WORKDIR /app
COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=builder /app/app.jar .

CMD ["java", "-jar", "app.jar"]
```

这个 Dockerfile 包含两个阶段：

```text
builder 阶段                   最终运行阶段
JDK                            JRE
  │                              │
  ├── COPY Hello.java            │
  ├── javac                       │
  └── jar → app.jar ─────────────┤ COPY --from=builder
                                 │
                                 └── CMD java -jar app.jar
```

### `FROM ... AS` 命名的是构建阶段

```dockerfile
FROM eclipse-temurin:21-jdk AS builder
```

`builder` 是这个构建阶段的名字。它不是某一个 Image Layer 的别名。

一个 Stage 内仍然可以由 `COPY`、`RUN` 等多个步骤产生多个层，而 `AS builder` 是给这一整段构建环境命名，方便后面的阶段引用。

如果不命名，也可以按序号引用阶段，但名字不会因为 Dockerfile 中插入新的阶段而改变，含义也更清楚。

## 3. `COPY --from` 只取需要的构建产物

第二个阶段执行：

```dockerfile
COPY --from=builder /app/app.jar .
```

这里有两套不同的文件系统视图：

- `/app/app.jar` 是 `builder` 阶段中的源文件。
- 目标 `.` 属于当前 JRE 运行阶段，并以当前 `WORKDIR /app` 为基准。

最终结果是：

```text
builder:/app/app.jar
          │
          │ COPY --from=builder
          ▼
final:/app/app.jar
```

编译阶段中的 `Hello.java`、`Hello.class`、`javac` 和其他 JDK 工具不会因为这条 `COPY` 一起进入最终镜像。只有明确复制的 `app.jar` 被带到运行阶段。

这与普通 `COPY` 的区别是：

```text
COPY source target
    source 来自 Build Context

COPY --from=builder source target
    source 来自另一个构建阶段的文件系统
```

## 4. 最终镜像到底保留什么

默认构建整个 Dockerfile 时，最后一个 Stage 会作为最终 Image 的输出。

这里需要区分 Stage 和 Layer，不能简单说“只有最后一层被保留”：

```text
最终 Image
    ├── 最后一个 Stage 的基础镜像层
    ├── WORKDIR 等运行配置
    ├── COPY 进来的 app.jar 层
    └── CMD 等 Image Config
```

最终运行镜像仍然包含最后一个 Stage 自己需要的多个层。没有进入最终镜像的是前面 `builder` Stage 的完整文件系统和构建工具。

所以，多阶段构建并不是把前面所有内容合并后再删除 JDK，而是从一个更轻的运行时基础镜像重新开始，只把需要的产物复制过来：

```text
单阶段
JDK + 源码 + class + JAR + 构建工具

多阶段最终结果
JRE + JAR
```

本机实验中两个镜像的大小是：

```text
java-lab:v1    约 746 MB
java-lab:v2    约 475 MB
```

大小下降并不是因为 JAR 变小了，而是最终镜像改用 JRE 作为基础，不再携带完整 JDK 构建环境。

## 5. 前面阶段没有进入 Image，缓存是否还在

前面的 `builder` Stage 不会整体成为最终 Image 的文件系统，但这不意味着每次构建都一定要从零开始编译。

BuildKit 可以把前面阶段各步骤的构建结果保存在缓存中。Dockerfile、源码和相关输入没有变化时，后续再次构建仍可能看到：

```text
CACHED
```

因此，下面两件事可以同时成立：

```text
builder Stage 不进入最终运行镜像
                ＋
builder Stage 的构建结果可以被缓存复用
```

不过，缓存不是最终镜像永久附带的内容。它属于当前构建器的缓存管理范围，可能因为手动清理、空间回收、切换 Builder 或更换机器而不存在。如果希望在其他机器或 CI 环境复用，还需要专门导入、导出或共享构建缓存。

所以更准确的说法不是“前面阶段的缓存永远不会丢失”，而是：

> 前面阶段虽然不进入最终 Image，但只要相关 Build Cache 仍然可用，就能继续帮助后续构建提速。

## 6. 用 History 验证最终镜像

构建多阶段版本：

```bash
docker build \
  --progress=plain \
  -t java-lab:v2 .
```

然后查看最终镜像历史：

```bash
docker image history java-lab:v2
```

本机结果的顶部主要是：

```text
CMD ["java" "-jar" "app.jar"]
COPY /app/app.jar .
WORKDIR /app
```

再往下是最终 JRE 基础镜像的历史，而不是 JDK 编译阶段中 `javac` 和 `jar` 产生的文件层。

这说明最终 Image 的确来自最后一个运行阶段，只接收了 `builder` 阶段输出的 `app.jar`。

还可以检查最终启动命令：

```bash
docker image inspect java-lab:v2 \
  -f '{{json .Config.Cmd}}'
```

输出：

```json
["java","-jar","app.jar"]
```

运行容器：

```bash
docker run --rm java-lab:v2
```

容器只需要运行已经构建好的 JAR，不需要在启动时重新编译源码。

## 7. 当前形成的理解

现在我把 Java 应用镜像的构建过程理解为两种职责：

```text
Build Stage
    ├── 需要 JDK
    ├── 编译源码
    └── 生成 JAR
             │
             │ 只传递构建产物
             ▼
Runtime Stage
    ├── 只需要 JRE
    ├── 保存 JAR
    └── 启动 Java 进程
```

单阶段 Dockerfile 虽然更短，但把“生成程序”和“运行程序”需要的工具全部放进了同一个最终镜像。

Multi-stage Build 则让不同阶段使用不同的 Base Image：编译阶段选择具备完整工具链的 JDK，运行阶段选择只满足程序运行需求的 JRE，并用 `COPY --from` 明确传递产物。

它带来的价值不只是镜像更小，也让最终运行环境更接近真正需要的内容：没有源码、编译器和其他只在构建时使用的文件，构建职责与运行职责更加清楚。

今天的 Java 容器构建实验还没有全部结束。后续完成剩余部分后，再继续补充到这篇笔记中。
