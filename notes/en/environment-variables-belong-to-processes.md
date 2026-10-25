# Who Owns an Environment Variable?

[中文](../environment-variables-belong-to-processes.md) | **English** | [日本語](../ja/environment-variables-belong-to-processes.md)

In a shell, you can write `NAME=Tom` or `export NAME=Tom`, or put the same line in `.bashrc` or `/etc/environment`. All of these look like "setting a variable," but they differ in who holds the variable, whether it reaches child processes, and when it goes away.

## Shell Variables vs. Environment Variables

Start by setting an ordinary variable in Bash:

```bash
NAME=Tom
echo "$NAME"
bash -c 'echo "child: ${NAME:-<empty>}"'
```

The current shell can read `NAME`, but the newly started Bash prints `<empty>`. At this point, `NAME` is just a variable managed by the current shell.

Mark it as an environment variable with `export` and try again:

```bash
export NAME=Tom
bash -c 'echo "child: $NAME"'
```

This time the child process prints `child: Tom`. The essential differences are:

- A shell variable exists only in the current shell's internal state.
- An exported environment variable is passed to child processes whenever the shell launches an external command.
- A child process receives a copy of the environment; changing its own variables never modifies the parent.

If a variable is needed by just one command, you can write:

```bash
APP_ENV=test bash -c 'echo "$APP_ENV"'
```

This assignment goes only into that command's environment and does not remain in the current shell.

## Environment Variables Are Not a Shell Feature

At its core, an environment variable is part of a **process's environment data**. When Linux starts a new program, the caller passes a list of `KEY=VALUE` strings through the `envp` argument of `execve()`, and the kernel hands them over to the new program. The shell merely provides convenient syntax (assignment, `export`, and variable expansion) and assembles the environment to pass along when it launches a command.

This means a process's environment can be set without going through a shell at all; Java, Python, systemd, and container runtimes each have their own way of configuring it. Conversely, shell-internal data such as arrays and functions generally cannot be passed to child processes as environment variables.

```text
Current shell
    │ launches a command, copying exported variables
    ▼
Child process
    │ launches another command
    ▼
Grandchild process
```

## Temporary Settings vs. Persistent Configuration

Running `export JAVA_HOME=...` in a terminal modifies the current shell process. Once that shell exits, the variable is gone.

"Persistent configuration" does not mean the kernel stores the variable permanently. It means the text that sets the variable is kept on disk. Whenever a login session or shell is created later, the relevant programs read the configuration files again and set up the new process's environment once more.

Common files and the conditions under which Bash typically loads them:

| File | Scope | Typically loaded for |
|---|---|---|
| `~/.bashrc` | Current user | Interactive, non-login Bash |
| `~/.bash_profile` | Current user | Login Bash; usually sources `~/.bashrc` explicitly |
| `~/.profile` | Current user | Generic login-shell configuration; Bash reads it only if neither `~/.bash_profile` nor `~/.bash_login` exists |
| `/etc/profile` | All users | System-wide initialization for login shells |
| `/etc/bash.bashrc` or `/etc/bashrc` | All users | Interactive Bash; the file name and loading mechanism vary by distribution |
| `/etc/environment` | System login environment | Usually read by login components such as PAM; not a Bash script |

When Bash starts as a login shell, it looks for `~/.bash_profile`, `~/.bash_login`, and `~/.profile`, in that order, and reads only the first one that exists. A common practice is to add the following to `~/.bash_profile`:

```bash
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi
```

`/etc/environment` should not be written as a shell script. It normally holds only simple key-value pairs, without `export`, command substitution, or complex variable expansion:

```text
JAVA_HOME=/usr/lib/jvm/java-21
APP_ENV=prod
```

The loading chain can differ across distributions, login methods, and service managers. For example, a systemd service does not automatically receive variables just because some user defined them in `.bashrc`. To determine whether a file takes effect, first identify who launches the target process and which configuration that launch reads.

## Production Scripts: Reusing Shared Configuration and Selecting an Environment

The layout below separates shared variables from per-environment differences:

```text
demo/
├── config/
│   ├── common.env
│   ├── dev.env
│   ├── test.env
│   └── prod.env
└── run.sh
```

`config/common.env`:

```bash
export APP_NAME=my-service
export LOG_LEVEL=info
```

`config/dev.env`:

```bash
export API_BASE_URL=http://localhost:8080
```

`config/test.env` and `config/prod.env` use the same variable names, each with its own URL. `run.sh` selects a configuration based on its first argument:

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

Run it from the `demo` directory:

```bash
chmod +x run.sh
./run.sh dev
./run.sh test
```

Here, `source` (which can also be written as `.`) executes the file in the **current shell**, so `run.sh` can use its variables afterward. Running `./config/dev.env` directly would instead create a separate process, which cannot modify `run.sh`'s variables.

`source` does not export anything by itself. The configuration files above use `export` explicitly, so child processes such as `bash -c` inherit their variables. If an existing configuration file contains only `KEY=value` lines, you can temporarily turn on automatic export while loading it:

```bash
set -a
source "$SCRIPT_DIR/config/common.env"
set +a
```

A sourced file can run arbitrary shell commands with the current script's privileges, so load only trusted files. Never commit secrets such as passwords or tokens to Git, and avoid passing them as command-line arguments whenever possible. In production, prefer configuration files with tightly controlled permissions or a dedicated secrets management service.

## Summary

An ordinary shell variable belongs to the current shell; an exported environment variable is copied to child processes. Configuration files can make variables "persistent" because each new session reads them again, not because Linux keeps a permanent, global environment. In real scripts, first confirm who loads the configuration, then use `source`, `export`, and an explicit environment-selection rule to control each variable's scope.
