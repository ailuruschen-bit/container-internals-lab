# Docker Image 基础操作：先学会查看镜像

[English](../docker-image-basics.md) | **中文** | [日本語](../ja/docker-image-basics.md)

今天开始学习 Docker Image。接触的内容还不多，主要是查看本地镜像、检查镜像的详细信息，以及删除不再需要的镜像。

这篇先记录目前已经实际理解的部分，后面学到镜像分层、拉取、标签和构建时再继续补充。

## 1. 查看本地镜像

```bash
docker image ls
```

这个命令列出 Docker Engine 当前保存的镜像。它也有一个常见简写：

```bash
docker images
```

输出大致如下：

```text
REPOSITORY   TAG       IMAGE ID       CREATED       SIZE
alpine       latest    294b683cb724   5 weeks ago   14.6MB
```

其中几个字段分别表示：

- `REPOSITORY`：镜像所属的仓库名称，例如 `alpine`。
- `TAG`：镜像标签，例如 `latest`。
- `IMAGE ID`：本地镜像的标识，默认只显示前一部分。
- `CREATED`：镜像的创建时间，不是我下载它的时间。
- `SIZE`：镜像在当前 Docker 环境中显示的大小。

镜像通常可以使用“仓库名:标签”引用：

```text
alpine:latest
nginx:alpine
```

如果省略标签，Docker 通常使用 `latest`。但 `latest` 只是一个普通的标签名，并不表示 Docker 会自动判断哪个版本最新。

需要查看完整 ID 时，可以使用：

```bash
docker image ls --no-trunc
```

只寻找某个镜像时，可以直接指定名称：

```bash
docker image ls alpine
```

这里得到的是 Docker Engine 本地已有的镜像，不是远程镜像仓库的搜索结果。

## 2. 查看一个镜像的详细信息

```bash
docker image inspect alpine
```

`inspect` 会返回一段 JSON。相比 `image ls` 的摘要，它更接近 Docker 实际保存的镜像元数据。

一个镜像也可以通过带标签的名称或镜像 ID 来检查：

```bash
docker image inspect alpine:latest
docker image inspect 294b683cb724
```

输出最外层是数组，因为这个命令可以一次检查多个镜像：

```bash
docker image inspect alpine nginx:alpine
```

### 我目前重点关注的字段

| 字段 | 我对它的理解 |
|---|---|
| `Id` | 本地镜像配置对象的完整内容标识 |
| `RepoTags` | 指向这个镜像的仓库名与标签 |
| `RepoDigests` | 镜像从仓库获取时对应的内容摘要引用 |
| `Created` | 镜像元数据中记录的创建时间 |
| `Os` | 镜像面向的操作系统，例如 `linux` |
| `Architecture` | 镜像面向的 CPU 架构，例如 `arm64` |
| `Size` | 镜像大小，JSON 中通常以字节表示 |
| `Config` | 镜像为容器准备的默认运行配置 |
| `RootFS.Layers` | 组成镜像根文件系统的层摘要 |

`Config` 中还可以继续看到一些重要信息：

- `Env`：镜像预设的环境变量。
- `Cmd`：没有另外指定命令时使用的默认命令。
- `Entrypoint`：镜像配置的入口程序。
- `WorkingDir`：容器启动后的默认工作目录。
- `User`：镜像默认使用的用户。
- `ExposedPorts`：镜像声明的服务端口。

并不是每个镜像都会把这些字段全部设置出来。没有配置的字段可能为空，也可能不出现在当前输出中。

`ExposedPorts` 只是镜像提供的信息，并不会自动把端口发布到宿主环境。真正建立端口映射仍然需要在运行容器时使用 `-p` 等配置。

这里让我重新确认了一点：镜像不只有一组文件，它还携带了创建容器时会用到的默认配置。`docker run` 启动容器时，既会使用镜像的文件系统内容，也会读取这些配置；命令行参数又可以覆盖其中一部分默认值。

## 3. 使用 `inspect -f` 提取需要的信息

完整 JSON 很详细，但有时我只想查看其中一个字段。这时可以使用 `-f`，它是 `--format` 的缩写：

```bash
docker image inspect -f '{{.Id}}' alpine
```

`-f` 后面使用的是 Go Template。最基本的写法是从 `.` 表示的当前对象开始，按照 JSON 字段层级向下访问。

例如，查看系统与 CPU 架构：

```bash
docker image inspect -f '{{.Os}}/{{.Architecture}}' alpine
```

输出可能是：

```text
linux/arm64
```

查看默认命令：

```bash
docker image inspect -f '{{json .Config.Cmd}}' alpine
```

查看默认工作目录：

```bash
docker image inspect -f '{{.Config.WorkingDir}}' alpine
```

查看镜像标签列表：

```bash
docker image inspect -f '{{json .RepoTags}}' alpine
```

这里使用 `json`，是为了让数组或嵌套对象以合法 JSON 的形式输出，而不是依赖模板的默认展示方式。

因此，我现在对 `inspect -f` 的理解是：

```text
inspect       取得完整对象信息
    ↓
--format      按模板选择和组织输出
    ↓
{{.Config.Cmd}}  沿字段层级取值
```

如果不确定字段路径，可以先查看一次完整的 `inspect` JSON，再根据它的嵌套结构编写模板。

## 4. 删除本地镜像

```bash
docker image rm alpine:latest
```

它还有两个等价写法：

```bash
docker image remove alpine:latest
docker rmi alpine:latest
```

删除命令可以接收镜像名称、带标签的名称或镜像 ID，也可以一次指定多个目标：

```bash
docker image rm alpine:latest nginx:alpine
```

这里删除的是当前 Docker Engine 中的本地镜像或标签，不会删除远程镜像仓库中的内容。

标签和镜像本身也要区分。如果同一个镜像还有其他标签引用它，删除其中一个标签可能只是解除这一个名称的引用，底层镜像数据不一定马上消失。

如果某个容器仍然依赖该镜像，Docker 可能拒绝删除并提示冲突。这时应该先确认相关容器是否还需要，而不是看到报错就直接使用强制删除：

```bash
docker ps -a
```

`docker image rm` 提供了 `-f` 或 `--force`，但它应该是明确理解引用关系之后的选择，而不是默认操作。

## 5. 今天形成的基础认识

现在我先把镜像理解为两部分：

```text
Docker Image
    ├── 文件系统内容
    └── 创建容器所需的默认配置
```

对应到今天使用的命令：

```text
docker image ls       查看本地有哪些镜像
docker image inspect  查看一个镜像记录了什么
inspect -f            从详细信息中提取需要的字段
docker image rm       删除本地镜像或标签
```

这只是 Image 学习的开头。镜像为什么会分层、标签和 Digest 有什么区别、拉取时实际下载了什么，以及 Dockerfile 怎样生成镜像，留到后续学习后继续补充。
