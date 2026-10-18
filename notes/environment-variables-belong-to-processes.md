# 环境变量到底属于谁？

在 Shell 中，我们既可以写 `NAME=Tom`，也可以写 `export NAME=Tom`；还可以把同样的内容放进 `.bashrc` 或 `/etc/environment`。这些写法看起来都在“设置变量”，但变量由谁保存、能否传给子进程、何时失效并不相同。

## Shell 变量与环境变量

先在 Bash 中设置一个普通变量：

```bash
NAME=Tom
echo "$NAME"
bash -c 'echo "child: ${NAME:-<empty>}"'
```

当前 Shell 能读到 `NAME`，新启动的 Bash 却输出 `<empty>`。`NAME` 此时只是当前 Shell 管理的变量。

用 `export` 将它标记为环境变量后再试：

```bash
export NAME=Tom
bash -c 'echo "child: $NAME"'
```

子进程会输出 `child: Tom`。两者的核心区别是：

- Shell 变量只存在于当前 Shell 的内部状态中。
- 导出的环境变量会在 Shell 启动外部命令时传给子进程。
- 子进程得到的是一份环境副本；它修改自己的变量，不会反向修改父进程。

如果变量只供一条命令使用，可以写成：

```bash
APP_ENV=test bash -c 'echo "$APP_ENV"'
```

这次赋值只进入该命令的环境，不会留在当前 Shell 中。

## 环境变量不是 Shell 独有的功能

环境变量本质上是**进程的环境数据**。Linux 启动新程序时，调用者通过 `execve()` 的 `envp` 参数传入一组 `KEY=VALUE` 字符串，内核再把它们交给新程序。Shell 只是提供了赋值、`export` 和变量展开等便捷语法，并在启动命令时组装要传递的环境。

因此，不经过 Shell 也可以为进程设置环境；Java、Python、systemd 和容器运行时都有自己的配置方式。反过来，Shell 中的数组、函数等内部数据通常不能作为环境变量传给子进程。

```text
当前 Shell
    │ 启动命令，并复制已导出的变量
    ▼
子进程
    │ 再启动命令
    ▼
孙进程
```

## 临时设置与持久配置

在终端中执行 `export JAVA_HOME=...`，修改的是当前 Shell 进程。关闭这个 Shell 后，变量也随之消失。

所谓“持久配置”并不是内核永久保存了变量，而是把设置变量的文本留在磁盘上。以后创建登录会话或 Shell 时，相关程序重新读取配置文件，再次设置新进程的环境。

常见文件及 Bash 下的典型加载条件如下：

| 文件 | 作用范围 | 典型加载条件 |
|---|---|---|
| `~/.bashrc` | 当前用户 | 交互式、非登录 Bash |
| `~/.bash_profile` | 当前用户 | 登录 Bash；通常在其中主动加载 `~/.bashrc` |
| `~/.profile` | 当前用户 | 登录 Shell 的通用配置；Bash 仅在 `~/.bash_profile` 和 `~/.bash_login` 都不存在时读取 |
| `/etc/profile` | 系统用户 | 登录 Shell 的系统级初始化 |
| `/etc/bash.bashrc` 或 `/etc/bashrc` | 系统用户 | 交互式 Bash；文件名与加载方式因发行版而异 |
| `/etc/environment` | 系统登录环境 | 通常由 PAM 等登录组件读取，不是 Bash 脚本 |

Bash 登录时，会按顺序查找 `~/.bash_profile`、`~/.bash_login`、`~/.profile`，并且只读取第一个存在的文件。常见做法是在 `~/.bash_profile` 中补上：

```bash
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi
```

`/etc/environment` 不应写成 Shell 脚本。通常只放简单的键值对，不使用 `export`、命令替换或复杂的变量展开：

```text
JAVA_HOME=/usr/lib/jvm/java-21
APP_ENV=prod
```

不同发行版、登录方式和服务管理器的加载链可能不同。例如 systemd 服务不会因为某个用户写了 `.bashrc` 就自动获得其中的变量。判断某个文件是否生效时，应先确认目标进程由谁启动、启动过程读取了哪些配置。

## 生产脚本：复用公共配置并选择环境

下面的目录把公共变量和各环境差异分开：

```text
demo/
├── config/
│   ├── common.env
│   ├── dev.env
│   ├── test.env
│   └── prod.env
└── run.sh
```

`config/common.env`：

```bash
export APP_NAME=my-service
export LOG_LEVEL=info
```

`config/dev.env`：

```bash
export API_BASE_URL=http://localhost:8080
```

`config/test.env` 和 `config/prod.env` 使用相同的变量名，填写各自的地址。`run.sh` 根据第一个参数选择配置：

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP_ENV=${1:-dev}

case "$APP_ENV" in
    dev|test|prod) ;;
    *)
        echo "usage: $0 {dev|test|prod}" >&2
        exit 2
        ;;
esac

source "$SCRIPT_DIR/config/common.env"
source "$SCRIPT_DIR/config/$APP_ENV.env"
export APP_ENV

bash -c 'printf "app=%s env=%s api=%s log=%s\n" \
    "$APP_NAME" "$APP_ENV" "$API_BASE_URL" "$LOG_LEVEL"'
```

在 `demo` 目录执行：

```bash
chmod +x run.sh
./run.sh dev
./run.sh test
```

这里的 `source`（也可写成 `.`）会在**当前 Shell** 中执行文件，因此 `run.sh` 随后能使用其中的变量。直接执行 `./config/dev.env` 则会创建另一个进程，不能修改 `run.sh` 的变量。

`source` 本身不会自动导出变量。上面的配置文件显式使用了 `export`，所以 `bash -c` 这样的子进程能够继承它们。如果已有配置文件只写 `KEY=value`，可以在加载期间临时开启自动导出：

```bash
set -a
source "$SCRIPT_DIR/config/common.env"
set +a
```

被 `source` 的文件会以当前脚本权限执行任意 Shell 命令，只应加载可信文件。密码、令牌等敏感信息不要提交到 Git，也尽量不要通过命令行参数传递；生产环境应优先使用权限受控的配置文件或专门的密钥管理服务。

## 小结

普通 Shell 变量属于当前 Shell；导出的环境变量可以复制给子进程。配置文件能够“持久化”变量，是因为新会话启动时会重新读取它们，而不是因为 Linux 保存了一份永久的全局环境。实际脚本中，先确认配置由谁加载，再用 `source`、`export` 和明确的环境选择规则控制变量的作用范围。
