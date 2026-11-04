# Docker Image 构建实操（二）：定制 Nginx 首页

[English](../docker-image-build-nginx.md) | **中文** | [日本語](../ja/docker-image-build-nginx.md)

上一次从 `alpine` 开始构建了一个最小的 Hello World 镜像。这一次不再从通用 Linux 基础镜像开始安装软件，而是直接以 `nginx:alpine` 为基础，构建自己的 Nginx Web Image。

这次 Dockerfile 只有两条指令，却让我更清楚地理解了两个问题：子镜像怎样继承基础镜像的运行配置，以及 `COPY` 怎样在不修改基础镜像的情况下改变最终文件系统视图。

## 1. 准备项目

项目目录可以保持得很简单：

```text
docker-image-lab/
└── 02-nginx/
    ├── Dockerfile
    └── index.html
```

`index.html` 是准备发布的自定义页面，例如：

```html
<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8">
    <title>My Nginx Image</title>
  </head>
  <body>
    <h1>Hello from my own Nginx image!</h1>
  </body>
</html>
```

Dockerfile：

```dockerfile
FROM nginx:alpine

COPY index.html /usr/share/nginx/html/index.html
```

与上一次相比，这次没有自己编写 `WORKDIR` 和 `CMD`。因为基础镜像已经准备好了 Nginx 程序、启动脚本、默认配置和默认网页目录，我只需要替换自己真正关心的页面内容。

## 2. 为什么可以不写 `CMD`

`FROM nginx:alpine` 不只是取得基础镜像中的文件系统层，也会以它的 Image Config 为起点。

本机当前 `nginx:alpine` 的关键配置包括：

```text
Entrypoint: ["/docker-entrypoint.sh"]
Cmd:        ["nginx", "-g", "daemon off;"]
Ports:      80/tcp
```

新的 Dockerfile 没有重新设置 `ENTRYPOINT` 或 `CMD`，所以构建出的 `my-nginx:v1` 会继续使用这些配置。

容器最终启动时，可以先简单理解为把二者组合起来：

```text
/docker-entrypoint.sh nginx -g "daemon off;"
└──── ENTRYPOINT ────┘ └──────── CMD ────────┘
```

入口脚本会先处理官方 Nginx 镜像提供的初始化逻辑，然后执行 `CMD` 给出的 Nginx 命令。`daemon off;` 让 Nginx 保持在前台运行，Nginx 进程不退出，容器也就能够继续保持运行状态。

所以，“继承 `CMD` 后自动启动 Nginx”这个理解方向是正确的，但更完整的说法是：**继承的 `ENTRYPOINT` 与 `CMD` 共同组成了容器的默认启动行为。**

### 80 端口来自哪里

镜像的配置中还能看到：

```json
{"80/tcp":{}}
```

它来自基础镜像的端口声明，用来说明该镜像预期在容器的 `80/tcp` 上提供服务。但这个声明本身不会让 Nginx 开始监听，也不会自动把容器端口发布到宿主机。

- Nginx 是否监听 80 端口，由 Nginx 配置决定。
- 镜像中的 `ExposedPorts` 是关于预期端口的元数据。
- 宿主机能否通过某个端口访问，还取决于运行容器时是否使用 `-p` 等配置。

## 3. 用 `COPY` 定制默认首页

官方 Nginx 镜像的默认静态资源目录是：

```text
/usr/share/nginx/html
```

其中已经存在默认的 `index.html`。本次构建执行：

```dockerfile
COPY index.html /usr/share/nginx/html/index.html
```

源文件来自本次 Build Context，目标路径则是新镜像文件系统中的默认首页位置。

从容器最终看到的目录视图来说，自定义页面替换了基础镜像的默认页面。但从镜像分层角度看，Docker 并没有回头修改 `nginx:alpine` 的原有层：

```text
my-nginx:v1
    ├── 新增层：自定义 index.html
    └── 基础层：nginx:alpine
                    └── 原默认 index.html
```

上层在相同路径提供了新文件，因此读取 `/usr/share/nginx/html/index.html` 时看到的是自定义内容。基础镜像中的旧文件仍然留在原来的只读层中，只是在最终合并视图里被上层文件遮住了。

这也意味着构建 `my-nginx:v1` 不会改变本地的 `nginx:alpine`。以后再次用原镜像启动容器，仍然会看到它自己的默认页面。

## 4. 构建自定义镜像

进入项目目录：

```bash
cd "$HOME/docker-image-lab/02-nginx"
```

构建镜像：

```bash
docker build -t my-nginx:v1 .
```

这里仍然使用当前目录作为 Build Context。构建器读取其中的 Dockerfile，并通过 `COPY` 把 `index.html` 加入新镜像。

构建完成后查看本地镜像：

```bash
docker image ls my-nginx
```

此时 `my-nginx:v1` 是新的 Image，`nginx:alpine` 仍然作为独立的基础 Image 保留在本地。

## 5. 使用 `inspect` 验证配置继承

查看完整镜像信息：

```bash
docker image inspect my-nginx:v1
```

只检查继承下来的 `CMD`：

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Cmd}}'
```

本次实验得到：

```json
["nginx","-g","daemon off;"]
```

这里的参数是 `-f`，即 `--format` 的缩写，不是 `if`。

只查看 `ENTRYPOINT`：

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Entrypoint}}'
```

输出：

```json
["/docker-entrypoint.sh"]
```

查看声明的端口：

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.ExposedPorts}}'
```

输出：

```json
{"80/tcp":{}}
```

这些结果说明：子镜像虽然只新增了一个页面文件，但基础镜像已经准备好的运行配置仍然存在。

如果子 Dockerfile 再写一条新的 `CMD`，它会替换基础镜像继承下来的 `CMD`，而不是同时保留两条默认命令。

## 6. 使用 `history` 观察构建结果

```bash
docker image history my-nginx:v1
```

输出最上方可以看到类似记录：

```text
COPY index.html /usr/share/nginx/html/index.html
```

它下面则是来自 `nginx:alpine` 的构建历史，包括安装 Nginx、设置环境变量、声明端口以及配置 `ENTRYPOINT`、`CMD` 等操作。

这与 Dockerfile 的继承关系正好对应：

```text
my-nginx:v1 的 COPY
          ↓
nginx:alpine 的已有历史
          ↓
更底层的 Alpine 镜像内容
```

`docker image history` 适合观察镜像大致由哪些构建步骤形成、每一步产生了多少大小。但它不是原始 Dockerfile 的完整备份，输出中的命令也可能经过构建器转换或截断。

需要查看更完整的创建命令时，可以使用：

```bash
docker image history --no-trunc my-nginx:v1
```

## 7. 启动并验证页面

将容器的 80 端口发布到本机 8080：

```bash
docker run -d --rm \
  --name my-nginx \
  -p 127.0.0.1:8080:80 \
  my-nginx:v1
```

访问页面：

```bash
curl http://localhost:8080
```

返回自定义 `index.html` 的内容，就说明：

- Nginx 使用继承的启动配置正常运行。
- Nginx 在容器中监听 80 端口。
- 自定义首页已经进入新镜像的文件系统。
- 宿主机 8080 到容器 80 的端口发布生效。

还可以直接进入容器检查最终文件：

```bash
docker exec my-nginx \
  cat /usr/share/nginx/html/index.html
```

实验结束后停止容器：

```bash
docker stop my-nginx
```

因为启动时使用了 `--rm`，容器停止后会自动删除，构建出的 `my-nginx:v1` 镜像仍然保留。

## 8. 今天形成的理解

这次实验中的继承与定制关系可以概括为：

```text
nginx:alpine
    ├── Nginx 程序与默认配置
    ├── 默认静态资源
    ├── ENTRYPOINT
    ├── CMD
    └── ExposedPorts
            │
            │ FROM
            ▼
my-nginx:v1
    ├── 继承基础镜像的文件与配置
    └── COPY 自定义 index.html 到新层
```

这让我对“以某个镜像为基础”有了更具体的认识：它不只是复用已经安装好的程序，也会继承镜像提供的默认运行配置。只要这些默认值符合需要，子 Dockerfile 就不必重复声明。

同时，所谓“替换基础镜像中的文件”，通常并不是修改原来的只读层，而是在新层的相同路径写入新内容，让它成为最终视图中可见的文件。

相比第一次从 Alpine 构建 Shell 程序，这一次更接近实际使用方式：选择一个已经完成通用配置的应用镜像，只把自己的业务内容叠加上去，最后得到可以重复运行的自定义 Image。
