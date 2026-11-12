# Docker Compose 配置实操：从 `.env` 到 Secrets

上一篇 Compose 笔记已经很长，因此我把环境配置和敏感信息单独拆出来记录。这次实验要解决的问题很具体：数据库名称、用户名可以作为普通配置传入容器，但密码不应该直接展开在 Compose 配置和容器环境变量中。

## 1. `.env` 解决的是配置替换

在 Compose Project 目录中准备 `.env`：

```dotenv
DB_NAME=labdb
DB_USERNAME=labuser
```

`compose.yaml` 可以使用 `${变量}` 引用这些值：

```yaml
services:
  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
```

Compose 执行命令时会读取默认 `.env`，先完成变量替换，再创建 Container。可以观察替换后的结果：

```bash
docker compose config
docker compose config --environment
```

如果把密码也写成 `${DB_PASSWORD}`，`docker compose config` 的结果中就会出现完整密码；将它传入 `environment` 后，密码还会成为容器环境变量。因此，`.env` 适合管理环境差异，却没有改变敏感值的传递方式。

需要从其他位置读取替换变量时，可以使用：

```bash
docker compose --env-file ./config/.env.dev up -d
```

Service 下的 `env_file` 则是另一种用途：它会把指定文件中的变量批量传入 Container，而不是专门为整个 `compose.yaml` 的 `${变量}` 提供替换值。

## 2. `.env.example` 只描述结构

实际 `.env` 应加入 `.gitignore`，仓库中只保留不含真实敏感值的模板：

```gitignore
.env
.env.*
!.env.example
```

```dotenv
# .env.example
DB_NAME=labdb
DB_USERNAME=labuser
```

新环境可以复制模板再填写自己的配置：

```bash
cp .env.example .env
```

这能避免本地配置被误提交，但 `.gitignore` 不会加密文件。已经提交过的密码不能靠后来删除来恢复安全，仍然需要及时更换。

## 3. Compose Secrets 的基本模型

Compose Secret 的使用分为两步：

1. 在顶级 `secrets` 中声明 Secret 名称和内容来源。
2. 在具体 Service 中显式授予访问权限。

```yaml
services:
  db:
    image: postgres:17-alpine
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

启动后，Compose 会把源文件以只读文件的形式挂载到被授权的 Container：

```text
宿主机 ./secrets/db_password.txt
              │
              ▼
Container /run/secrets/db_password
```

顶级声明并不会让所有 Service 自动获得 Secret。只有写了 `secrets` 的 Service 才会得到相应挂载。

Secret 也不会自动变成环境变量。容器中的程序必须读取这个文件，或者自身提供“从文件读取配置”的功能。

## 4. PostgreSQL：通过 `_FILE` 读取密码

PostgreSQL 官方 Image 的启动脚本支持 `POSTGRES_PASSWORD_FILE`。因此只要告诉它 Secret 文件在哪里，不需要自己再写读取脚本：

```yaml
services:
  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
      POSTGRES_PASSWORD_FILE: /run/secrets/db_password
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

这里的环境变量只保存文件路径，真正的密码仍然位于挂载文件中：

```text
POSTGRES_PASSWORD_FILE=/run/secrets/db_password
                                      │
                                      ▼
                              PostgreSQL 启动脚本读取
```

`_FILE` 是部分 Image 采用的约定，并不是 Docker 自动为所有环境变量提供的能力。使用其他 Image 时，需要先确认它是否支持这种写法。

还要注意：`POSTGRES_PASSWORD_FILE` 主要参与空数据目录的首次初始化。如果命名 Volume 中已经存在数据库，仅仅修改 Secret 文件并不会自动修改现有数据库用户的密码。

## 5. Spring Boot：通过 Config Tree 读取 Secret

自己的 Spring Boot 应用没有 `SPRING_DATASOURCE_PASSWORD_FILE`，但 Spring Boot 支持把一个目录作为 Config Tree 导入。

Config Tree 会把目录中的文件名当作配置项名称，把文件内容当作配置值。因此，可以用 Secret 的 `target` 改变容器内的文件名：

```yaml
services:
  app:
    image: spring-lab:dev
    environment:
      SPRING_DATASOURCE_URL: jdbc:postgresql://db:5432/${DB_NAME}
      SPRING_DATASOURCE_USERNAME: ${DB_USERNAME}
      SPRING_CONFIG_IMPORT: configtree:/run/secrets/
    secrets:
      - source: db_password
        target: spring.datasource.password
```

挂载结果是：

```text
/run/secrets/spring.datasource.password
              │                    │
              │                    └── 文件内容成为配置值
              └── 文件名成为 spring.datasource.password
```

Spring Boot 导入 `/run/secrets/` 后，就能获得与下面环境变量等价的配置值，但密码本身不需要进入环境变量：

```text
SPRING_DATASOURCE_PASSWORD=<密码>
```

同一个 `db_password` Secret 可以在不同 Service 中使用不同的目标文件名。PostgreSQL 保留默认的 `db_password`，Spring Boot 则通过 `target` 将它重命名为能被 Config Tree 识别的配置键。

## 6. 组合后的 Compose 配置

这次实验中与配置相关的部分可以组合为：

```yaml
services:
  app:
    image: spring-lab:dev
    environment:
      SPRING_DATASOURCE_URL: jdbc:postgresql://db:5432/${DB_NAME}
      SPRING_DATASOURCE_USERNAME: ${DB_USERNAME}
      SPRING_CONFIG_IMPORT: configtree:/run/secrets/
    secrets:
      - source: db_password
        target: spring.datasource.password

  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
      POSTGRES_PASSWORD_FILE: /run/secrets/db_password
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

对应关系是：

```text
.env
  ├── DB_NAME      ──→ Compose 替换 ──→ Container 环境变量
  └── DB_USERNAME  ──→ Compose 替换 ──→ Container 环境变量

db_password.txt
  └── Compose Secret
          ├── db  /run/secrets/db_password
          └── app /run/secrets/spring.datasource.password
```

## 7. Secrets 的安全边界

使用 Secrets 后，执行 `docker compose config` 只会看到 Secret 的文件来源和挂载关系，不会把文件内容展开到配置中；密码也不会出现在 Container 的环境变量列表里。这比直接使用 `${DB_PASSWORD}` 更不容易在配置输出、环境变量检查或调试日志中意外暴露。

但在普通本地 Compose 中，`file` 指向的源文件仍然以明文存在于宿主机。能够读取该文件、控制 Docker Engine，或进入获授权 Container 的用户，仍然可能读取密码。因此还需要：

- 将真实 Secret 文件加入 `.gitignore`，只提交 `.example` 模板。
- 限制宿主机文件权限和 Docker 管理权限。
- 不把 Secret 写进 Dockerfile、Image Layer 或应用日志。

Secrets 改善的是敏感信息进入容器的方式，并不等于完整的密钥托管、加密和轮换系统。

## 8. 当前理解

```text
普通环境配置
    └── .env ──→ ${变量} 替换 ──→ Compose 配置或 Container 环境变量

敏感配置
    └── Secret 源文件 ──→ /run/secrets/... ──→ 应用在运行时读取
```

这次实验让我把“配置”和“秘密”区分开来：两者都需要传入容器，但不应该使用完全相同的传递方式。`.env` 让配置更容易按环境切换，Secrets 则尽量让密码只以文件形式出现在确实需要它的 Container 中。
