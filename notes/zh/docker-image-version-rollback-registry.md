# Docker Image 实操（五）：版本替换、回滚与 Registry

完成 Spring Boot 镜像构建后，我继续实验了两个更接近交付过程的问题：应用版本怎样更新和回滚，以及构建完成的 Image 怎样在不同环境之间传递。

这次最重要的认识是：容器化应用通常不在原容器中更新程序，而是用新 Image 创建新 Container；Registry 则负责保存和分发这些不同版本的 Image。

## 1. 版本更新不是修改原容器

本次先后构建了两个 Spring Boot 镜像：

```text
spring-lab:v1
spring-lab:v2
```

如果生产容器当前运行 `v1`，发布 `v2` 的基本思路不是进入旧容器覆盖 JAR，而是停止并移除旧容器，再用新镜像创建容器：

```bash
docker stop spring-prod
docker rm spring-prod

docker run -d \
  --name spring-prod \
  -p 127.0.0.1:8081:8080 \
  spring-lab:v2
```

此时发生的是运行实例替换：

```text
旧容器：spring-lab:v1  ──→  停止并删除

新容器：spring-lab:v2  ──→  创建并启动
```

原来的 `spring-lab:v1` Image 仍然可以保留。只要需要的运行配置仍然兼容，就可以用同样方式停止 `v2`，重新从 `v1` 创建容器，实现应用版本回滚。

本机实验完成后，`spring-prod` 当前运行的是：

```text
spring-lab:v2
```

## 2. 为什么无状态服务容易替换

如果应用实例不把需要长期保留的业务状态写在自己的容器可写层中，那么容器本身就可以被替换：

```text
Container
    ├── 应用程序：来自 Image
    ├── 运行配置：启动时注入
    └── 持久化状态：外部数据库、Volume 或其他服务
```

删除一个应用容器后，新的容器可以重新读取外部状态并继续提供服务。对于这样的无状态应用，版本更迭主要变成“选择哪个 Image 创建新实例”。

但“只替换镜像就可以”仍有几个成立条件：

- 配置、密码和环境差异能够在运行时重新提供。
- 数据没有只保存在旧容器的可写层中。
- 新旧版本使用的外部数据结构彼此兼容。
- 旧版本 Image 仍然可以取得。

尤其需要注意，镜像回滚只能回滚应用程序，并不会自动回滚数据库。若新版本执行了不兼容的数据库迁移，即使重新运行旧 Image，旧程序也未必还能正常工作。

### 简单替换不等于无停机发布

单机上只有一个容器，而且新旧容器都要占用宿主机同一个端口时，必须先停止旧容器才能启动新容器，中间可能产生短暂停机。

如果希望发布期间持续提供服务，通常需要：

- 同时运行多个应用副本。
- 让新旧版本使用不同的内部实例或端口。
- 对新实例执行健康检查。
- 通过负载均衡或编排系统逐步切换流量。

因此，容器让版本替换变得简单，但滚动发布、自动回滚和零停机仍然需要更上层的部署机制。

## 3. Tag 与可回滚版本

`v1`、`v2` 让版本关系比较直观：

```text
spring-lab:v1
spring-lab:v2
```

但 Tag 本质上是可以重新指向其他镜像内容的名称。若重新构建并再次使用相同的 `v2` Tag，后来看到的 `v2` 未必还是第一次发布时的内容。

为了让回滚更可靠，可以：

- 为每次发布使用不会重复的版本 Tag，例如版本号或提交编号。
- 保存部署时实际使用的 Image Digest。
- 避免只依赖含义模糊且可变化的 `latest`。

Tag 方便人阅读，Digest 更适合精确确认镜像内容。无论使用哪种方式，前提都是旧版本仍被保存在本地或 Registry 中，而没有被清理策略删除。

## 4. Registry 解决了什么问题

只存在于开发机 Docker Engine 中的镜像，其他服务器无法直接使用。Registry 是一个镜像存储和分发服务，Docker 客户端可以向它 Push Image，也可以从中 Pull Image。

三者的关系可以简单表示为：

```text
开发或 CI 环境
    │ docker push
    ▼
Registry
    │ docker pull
    ▼
测试或生产环境
```

Registry 不负责编译应用，也不会因为收到新 Image 就自动替换正在运行的容器。它负责的是保存、命名和分发镜像内容；构建与部署仍由 Docker 客户端、CI/CD 或编排平台完成。

## 5. 在本地运行 Registry

本次实验直接使用官方 `registry:3` Image：

```bash
docker run -d \
  --name registry-lab \
  -p 127.0.0.1:5000:5000 \
  registry:3
```

容器内的 Registry 监听 5000，OrbStack 再把它发布到 macOS 的 `127.0.0.1:5000`。

可以通过 Registry API 查看本次实验中的仓库：

```bash
curl http://127.0.0.1:5000/v2/_catalog
```

本机当前返回：

```json
{"repositories":["spring-lab"]}
```

再查看 `spring-lab` 中的 Tag：

```bash
curl http://127.0.0.1:5000/v2/spring-lab/tags/list
```

当前结果是：

```json
{"name":"spring-lab","tags":["v1"]}
```

这些 HTTP API 适合观察实验结果，日常上传和下载镜像仍然使用 `docker push` 与 `docker pull`。

## 6. 为什么先给 Image 增加 Registry 前缀

一个完整镜像引用可以简化为：

```text
[Registry 地址/][命名空间/]Repository[:Tag]
```

本地镜像原本叫：

```text
spring-lab:v1
```

要推送到本地 Registry，需要给同一个 Image 增加带服务地址的 Tag：

```bash
docker image tag \
  spring-lab:v1 \
  localhost:5000/spring-lab:v1
```

这里的 `localhost:5000` 告诉 Docker 目标 Registry 在哪里，后面的 `spring-lab:v1` 则表示该 Registry 中的 Repository 和 Tag。

`docker image tag` 只是在本地增加另一个引用名称，不会上传镜像，也不会复制一份完整文件系统：

```text
spring-lab:v1
          └──────────────┐
                         ▼
                   同一个 Image ID
                         ▲
localhost:5000/spring-lab:v1
          └──────────────┘
```

## 7. Push、删除与 Pull

添加 Registry Tag 后，使用的是 `push`：

```bash
docker push localhost:5000/spring-lab:v1
```

不是 `pull`。两条命令的方向相反：

```text
docker push  本地 Docker Engine ──→ Registry
docker pull  Registry ──→ 本地 Docker Engine
```

Push 时，Registry 保存镜像 Manifest、配置和文件系统 Layers。内容相同且 Registry 已经拥有的 Layer 不需要每次重复保存，因此输出中可能看到某些层已经存在。

### 删除本地镜像后重新拉取

确认没有容器依赖这些镜像后，可以删除本地引用：

```bash
docker image rm localhost:5000/spring-lab:v1
docker image rm spring-lab:v1
```

这里要删除两个 Tag，原因是它们原本可能都指向同一个本地 Image。若只删除 Registry Tag，短名称 `spring-lab:v1` 仍然保留，底层镜像数据也可能继续存在，本次 Pull 就不能充分证明 Registry 具备恢复镜像的能力。

从 Registry 拉取：

```bash
docker pull localhost:5000/spring-lab:v1
```

拉取回来的是带完整 Registry 地址的引用：

```text
localhost:5000/spring-lab:v1
```

如果还希望继续使用原来的短名称，可以重新添加 Tag：

```bash
docker image tag \
  localhost:5000/spring-lab:v1 \
  spring-lab:v1
```

删除本地 Image 不会删除 Registry 中已经 Push 的内容；反过来，删除 Registry 容器或它的存储，则可能让远端内容丢失。

## 8. Registry 的数据也需要持久化

Registry 将数据保存在容器的 `/var/lib/registry`。本次运行的官方镜像为该路径使用了 Docker Volume，因此容器文件系统之外存在独立存储。

为了让存储名称和生命周期更明确，可以在创建 Registry 时指定命名 Volume：

```bash
docker volume create registry-data

docker run -d \
  --name registry-lab \
  -p 127.0.0.1:5000:5000 \
  --mount type=volume,source=registry-data,target=/var/lib/registry \
  registry:3
```

这样替换 Registry 容器时，可以重新挂载同一个 `registry-data`。不过 Volume 只解决单机实验中的容器数据独立问题，不等于已经具备备份、高可用和灾难恢复能力。

## 9. 从本地实验走向多环境

本地使用 `localhost:5000`，只有当前主机能够把这个地址理解为自己的 Registry。要让开发、测试和生产环境共享镜像，Registry 必须拥有这些环境都能访问的网络地址，例如：

```text
registry.example.com/team/spring-lab:v1
```

不同环境使用同一个完整 Image 引用，就可以围绕同一个构建产物完成测试和部署：

```text
构建一次
    ↓
Push 到 Registry
    ↓
测试环境 Pull 并验证
    ↓
生产环境 Pull 同一版本或 Digest
```

这里迁移的不是开发机上正在运行的 Container，而是不可变的 Image 内容。每个环境仍然要提供自己的环境变量、Secret、网络和持久化存储。

本地无认证的 HTTP Registry 只适合实验。真正对其他机器开放时，至少需要考虑：

- TLS 加密与可信证书。
- 登录认证和细粒度访问权限。
- 镜像保留与垃圾回收策略。
- 持久化存储、备份与可用性。
- 安全扫描、审计和网络访问控制。

企业可以自行维护 Distribution Registry，也可以使用成熟的托管服务，例如 AWS ECR。托管服务减少了 Registry 服务器、存储和可用性方面的维护工作，并能与云平台的身份权限、审计及扫描能力结合；Docker 客户端依然通过认证、Tag、Push 和 Pull 完成基本交互。

## 10. 今天形成的理解

这次实验把 Image 从“本机的构建产物”变成了“可以跨环境交付的版本”：

```text
源码
  │ build
  ▼
spring-lab:v1
  │ tag + push
  ▼
Registry
  │ pull
  ├──────────→ 测试环境
  └──────────→ 生产环境
                  │
                  ├── 用 v2 创建新容器：升级
                  └── 用 v1 创建新容器：回滚
```

对于无状态应用，容器本身可以随时被新版本替换，真正需要长期保存的状态位于容器之外。Registry 则保证部署环境能够取得指定的 Image，而不必在每台服务器上重新编译项目。

因此，完整的版本管理不只是保留几个本地 Tag，而是同时管理：

- 每个版本对应的明确 Image 引用或 Digest。
- Registry 中仍然可取得的历史镜像。
- 与该版本匹配的运行配置。
- 无法通过镜像回滚的数据结构变化。
- 从新容器启动到流量切换的发布流程。

镜像替换让应用升级和回滚的动作变得简单，但可靠发布仍然依赖版本不可混淆、状态外置、数据兼容，以及 Registry 和部署流程本身的稳定性。
