# Docker Image 构建实操（四）：Java 与 Multi-stage Build

[English](../12-docker-image-build-java-multistage.md) | **中文** | [日本語](../ja/12-docker-image-build-java-multistage.md)

这轮实验开始构建 Java 应用镜像：先在一个镜像中完成编译与运行，再使用 Multi-stage Build 把编译环境和运行环境分开，最后继续观察 `ENTRYPOINT` 的参数传递，并把同样的构建方法应用到一个最小 Spring Boot 项目。

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

在这个基础上，我继续把 Java 启动命令从 `CMD` 移到 `ENTRYPOINT`，并尝试构建实际的 Spring Boot 项目。

## 8. 用 `ENTRYPOINT` 固定 Java 启动程序

前一个版本使用：

```dockerfile
CMD ["java", "-jar", "app.jar"]
```

如果在镜像名称后继续传入参数，这些参数会直接替换整个 `CMD`：

```bash
docker run --rm java-lab:v2 hello
```

此时 Docker 会尝试把 `hello` 当成容器要执行的程序，而不是把它交给 `app.jar`。

为了固定 Java 程序本身，同时保留可变参数，可以把 Dockerfile 改成：

```dockerfile
ENTRYPOINT ["java", "-jar", "app.jar"]
CMD ["default"]
```

这两项使用 Exec Form 时，默认启动命令可以理解为：

```text
ENTRYPOINT                         CMD
["java", "-jar", "app.jar"]  +  ["default"]
                    │
                    ▼
java -jar app.jar default
```

`ENTRYPOINT` 固定要运行的程序，`CMD` 在这里提供默认参数。

### 使用默认参数

```bash
docker run --rm java-lab:v3
```

本机实验输出：

```text
Java application started!
Argument: default
```

### 在 `docker run` 中传入新参数

```bash
docker run --rm java-lab:v3 hello
```

输出变成：

```text
Java application started!
Argument: hello
```

镜像名称后面的 `hello` 替换了 `CMD ["default"]`，再作为参数追加给原有的 Exec Form `ENTRYPOINT`：

```text
java -jar app.jar hello
```

因此，更准确的说法不是“`docker run` 后面的内容总会传给 Java”，而是：

> 镜像具有 Exec Form `ENTRYPOINT` 时，运行命令提供的参数会替换 `CMD`，并作为参数交给 `ENTRYPOINT`。

## 9. 使用 `--entrypoint` 覆盖默认入口

如果连默认程序也要替换，可以使用：

```bash
docker run --rm \
  --entrypoint java \
  java-lab:v3 \
  -version
```

这次容器执行的是：

```text
java -version
```

而不是：

```text
java -jar app.jar default
```

`--entrypoint java` 为这次新容器覆盖镜像中的 `ENTRYPOINT`。使用这个选项时，镜像原有的 `CMD` 不会再自动追加；镜像名称后的 `-version` 成为新入口的参数。

这里的“覆盖”和“清除 CMD”都发生在本次容器配置中，并不会修改原来的 `java-lab:v3` Image。下一次正常运行该镜像，仍然会使用原有的 `ENTRYPOINT` 与 `CMD`。

如果要检查镜像中保存的两项配置，可以执行：

```bash
docker image inspect java-lab:v3 \
  -f 'Entrypoint={{json .Config.Entrypoint}} Cmd={{json .Config.Cmd}}'
```

本机结果是：

```text
Entrypoint=["java","-jar","app.jar"] Cmd=["default"]
```

### `CMD` 与 `ENTRYPOINT` 的当前理解

| 配置 | 在本次实验中的职责 |
|---|---|
| `ENTRYPOINT` | 固定容器主要执行的 Java 程序 |
| `CMD` | 为入口程序提供可替换的默认参数 |
| 镜像名后的参数 | 替换 `CMD`，交给默认 `ENTRYPOINT` |
| `--entrypoint` | 为本次容器替换默认入口，原 `CMD` 不再自动参与 |

这种组合适合“程序固定、参数经常变化”的镜像。它并不意味着所有镜像都必须同时使用 `ENTRYPOINT` 和 `CMD`，仍然要根据希望用户覆盖程序还是只覆盖参数来决定。

## 10. 把 Multi-stage Build 用到 Spring Boot

完成普通 Java JAR 的实验后，我又构建了一个最小 Spring Boot 项目。

Spring Boot 项目不再直接调用 `javac` 和 `jar`，而是通过 Maven 解析依赖、编译源码、运行构建生命周期并生成可执行 JAR。因此，构建阶段选择带有 Maven 和 JDK 的基础镜像，运行阶段仍然只使用 JRE。

项目结构可以简化为：

```text
spring-lab/
├── Dockerfile
├── pom.xml
└── src/
    └── main/
        ├── java/
        └── resources/
```

对应的 Dockerfile 大致是：

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build

WORKDIR /build

COPY pom.xml .
RUN mvn dependency:go-offline

COPY src ./src
RUN mvn clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080

ENTRYPOINT ["java", "-jar", "app.jar"]
```

这仍然是前面已经理解的两个阶段：

```text
Maven + JDK Build Stage
    ├── 读取 pom.xml
    ├── 下载依赖
    ├── 编译 Spring Boot 项目
    └── target/*.jar
                 │
                 │ COPY --from=build
                 ▼
JRE Runtime Stage
    ├── app.jar
    └── java -jar app.jar
```

## 11. Maven 在这个 Spring Boot 项目中负责什么

这次 Spring Boot 项目采用 Maven，因此构建过程高度依赖 Maven，但这不等于 Spring Boot 只能使用 Maven。Spring Boot 同样支持 Gradle；更准确的说法是：**当前这个以 `pom.xml` 管理的项目，其依赖解析和构建流程由 Maven 驱动。**

Maven 读取 `pom.xml` 后，会先组合出当前项目真正生效的构建模型。其中比较重要的来源包括：

- 项目自身声明的坐标、属性和构建配置。
- 通过 `<parent>` 继承的父 POM 配置。
- `<dependencyManagement>` 提供的依赖版本管理。
- `<dependencies>` 声明的直接依赖及其传递依赖。
- `<build>` 中配置或继承得到的 Maven Plugin。

如果项目继承 `spring-boot-starter-parent`，就可以取得 Spring Boot 提供的一组默认设置，例如受管理的常用依赖版本、编译参数和部分 Plugin 配置。这里的“继承”不是 Java 类继承，而是 Maven 在解析 POM 时把父子配置合成为 Effective POM。

`<dependencies>` 中声明的也不宜统一称为“模块”。Maven 处理的是构件（Artifact），常见形式是 JAR。一个直接依赖还可能继续引用其他构件，Maven 会按照依赖传递、版本管理和 Scope 等规则计算最终使用的依赖集合。

解析时，Maven 通常先检查本地仓库；本地不存在的 POM、JAR 和 Plugin 再从配置的远程仓库下载。默认本地仓库位于：

```text
~/.m2/repository
```

因此 Maven 不只是“下载库”，还负责理解 POM、解析依赖关系、执行生命周期与 Plugin，并最终生成构建产物。

对于 Spring Boot 项目，`mvn package` 会执行 Maven 到 `package` 阶段所需的生命周期步骤。配置了 Spring Boot Maven Plugin 后，还会把普通 JAR 重新组织成可以通过下面命令启动的可执行 JAR：

```text
java -jar app.jar
```

## 12. 两种 Maven Build Stage 的选择

完成这个实验后，我发现 Build Stage 至少有两种常见写法。二者最终都使用 Maven 构建项目，区别在于 Maven 由基础镜像提供，还是由项目自己的 Wrapper 启动。

### 方式一：使用包含 Maven 的基础镜像

当前实验采用：

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build
```

这个基础镜像已经准备好了 Maven 和 JDK，因此可以直接执行：

```dockerfile
RUN mvn dependency:go-offline
RUN mvn clean package -DskipTests
```

这里调用的是基础镜像中安装的全局 `mvn`。它的优点是 Dockerfile 简单，不需要先启动 Wrapper；Maven 版本则由选择的 Maven Image Tag 决定。

这种方式仍然应该使用明确的基础镜像版本。如果一直使用会变化的宽泛 Tag，不同时间重新构建时，实际 Maven 或 JDK 版本仍可能发生变化。

### 方式二：在 JDK 镜像中使用 Maven Wrapper

启用了 Maven Wrapper 的项目通常包含：

```text
spring-lab/
├── .mvn/
│   └── wrapper/
│       └── maven-wrapper.properties
├── mvnw
├── mvnw.cmd
├── pom.xml
└── src/
```

其中：

- `mvnw` 是 Linux 和 macOS 使用的启动脚本。
- `mvnw.cmd` 是 Windows 使用的启动脚本。
- `.mvn/wrapper/maven-wrapper.properties` 记录 Wrapper 使用的 Maven 发行版及下载地址等配置。
- 不同 Wrapper 类型还可能包含 `maven-wrapper.jar` 或下载器源码。

Wrapper 并不是把完整 Maven 安装包直接提交进项目。执行 `./mvnw` 时，它会根据项目内的 Wrapper 配置寻找指定版本；本地没有时，再下载并使用该 Maven 发行版。

在 Multi-stage Dockerfile 中，可以改用普通 JDK 镜像：

```dockerfile
FROM eclipse-temurin:21-jdk AS build

WORKDIR /build

COPY .mvn .mvn
COPY --chmod=0755 mvnw .
COPY pom.xml .

RUN ./mvnw dependency:go-offline

COPY src ./src
RUN ./mvnw clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app
COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
```

这里没有执行全局 `mvn`，而是始终执行项目提供的 `./mvnw`。这样开发机、CI 和 Docker Build Stage 都可以按照同一份 Wrapper 配置使用指定 Maven 版本。

`mvnw` 必须具有执行权限。示例使用 `COPY --chmod=0755` 在复制时设置权限；也可以在版本库中正确保存它的可执行位。

Wrapper 首次运行通常还需要网络下载 Maven 发行版。具体 Wrapper 类型可能依赖 Wrapper JAR、Java 下载器，或者 `curl`、`wget` 等工具，因此不能假设任意精简 JDK 镜像都一定具备所有启动条件。选择 Base Image 后仍要实际验证 Wrapper 能否正常启动。

### Maven 发行版与项目依赖不是同一个缓存目录

Wrapper 下载的 Maven 发行版本身，默认保存在：

```text
~/.m2/wrapper/dists
```

Maven 解析出的项目依赖和 Plugin 则默认保存在：

```text
~/.m2/repository
```

如果 Docker Build Stage 默认以 root 用户执行，`~` 通常是 `/root`，对应路径就是：

```text
/root/.m2/wrapper/dists
/root/.m2/repository
```

如果基础镜像切换了构建用户，或者通过 Maven 参数修改了本地仓库位置，实际路径也会改变，因此 `~/.m2/repository` 是默认约定，不是不可改变的固定地址。

无论使用基础镜像中的全局 Maven，还是使用 Maven Wrapper，依赖解析后都会使用 Maven Local Repository 的机制。区别主要在于“由谁决定并提供 Maven 本身”：

| 构建方式 | Maven 从哪里来 | Maven 版本主要由什么决定 |
|---|---|---|
| Maven 基础镜像 | 已安装在 Image 中 | Base Image Tag |
| Maven Wrapper | Wrapper 按项目配置取得 | `.mvn/wrapper` 配置 |

这两个方案都只用于 Build Stage。因为最终阶段只复制 JAR，所以 Maven、Wrapper、`~/.m2` 和项目源码都不会自动进入最终 JRE Image。

## 13. 为什么先复制 `pom.xml` 再复制源码

构建阶段没有一开始就执行 `COPY . .`，而是先处理依赖描述文件：

```dockerfile
COPY pom.xml .
RUN mvn dependency:go-offline
```

然后才复制经常修改的源码：

```dockerfile
COPY src ./src
RUN mvn clean package -DskipTests
```

这与上一篇 Build Cache 实验直接对应。

`pom.xml` 不变而 Java 源码发生变化时，下载依赖的构建步骤仍有机会命中缓存，只需要重新复制源码并执行打包。若一开始就把整个项目复制进去，任何源码变化都会改变前面的 `COPY` 结果，使后续依赖下载步骤也更容易失去缓存。

可以把两种顺序对比为：

```text
不利于复用缓存                 更利于复用缓存

COPY . .                       COPY pom.xml .
RUN mvn package                RUN mvn dependency:go-offline
                               COPY src ./src
                               RUN mvn package
```

`dependency:go-offline` 会提前解析并下载项目依赖与相关插件，让它们进入该构建步骤的缓存结果。它不是说最终运行容器必须离线，也不会把完整 Maven 环境复制进最终 JRE Image。

如果 `pom.xml` 改变，依赖输入本身已经变化，重新执行依赖下载就是合理的缓存失效。

## 14. Spring Boot 最终镜像保留了什么

第二个 `FROM` 开始了全新的运行阶段：

```dockerfile
FROM eclipse-temurin:21-jre
```

最终镜像不会整体包含 Maven、项目源码和 Build Stage 中的本地依赖仓库。这里只从前一个阶段复制打包产物：

```dockerfile
COPY --from=build /build/target/*.jar app.jar
```

本机 `spring-lab:v1` 的最终 History 顶部主要包含：

```text
COPY /build/target/*.jar app.jar
EXPOSE 8080
ENTRYPOINT ["java" "-jar" "app.jar"]
```

复制进最终镜像的 JAR 约为 23 MB，整个 `spring-lab:v1` Image 约为 519 MB。镜像的大部分体积来自 JRE 基础镜像，而不是 Maven Build Stage。

`EXPOSE 8080` 只是说明该镜像预期在容器的 `8080/tcp` 上提供服务，不会自动把端口发布到宿主机。运行时仍要明确配置：

```bash
docker run -d \
  --name springboot1 \
  -p 127.0.0.1:8081:8080 \
  spring-lab:v1
```

这里把 macOS 的 `8081` 发布到容器的 `8080`。查看日志：

```bash
docker logs springboot1
```

本机日志显示内置 Tomcat 已在容器的 8080 端口启动。访问根路径时：

```bash
curl -i http://localhost:8081
```

当前最小项目返回了 HTTP 404。这并不表示容器或 Tomcat 启动失败，而是应用还没有为 `/` 定义对应的 Controller；端口连通和 Spring Boot 启动本身已经得到验证。

## 15. 这轮 Java 构建实验形成的完整认识

从最初的单阶段 Java 构建，到 Spring Boot 的 Maven Multi-stage Build，这轮实验逐步拆开了四种职责：

```text
Build Tool
    Maven / javac / jar
            │
            ▼
Build Artifact
    可执行 JAR
            │
            │ COPY --from
            ▼
Runtime Environment
    JRE
            │
            ▼
Container Process
    ENTRYPOINT + CMD 或运行参数
```

现在我对 Java 应用容器化的理解不再是“把 Java 项目目录复制进镜像再启动”，而是：

1. 在 Maven + JDK 基础镜像或 JDK + Maven Wrapper 的 Build Stage 中生成经过构建的 JAR。
2. 只把 JAR 传递到更精简的 Runtime Stage。
3. 用 `ENTRYPOINT` 固定 Java 启动程序，并按需要用 `CMD` 提供默认参数。
4. 利用 Dockerfile 的顺序，让 Maven 依赖下载尽可能复用 Build Cache。
5. 通过运行时端口发布和外部配置，把同一个镜像用于不同环境。

Multi-stage Build 解决的是构建环境与运行环境的分离，`ENTRYPOINT` 和 `CMD` 解决的是容器主程序与默认参数的组合，而 Maven 依赖步骤的顺序则影响重复构建效率。这几部分组合起来，才构成一个比较完整的 Java 应用镜像构建过程。
