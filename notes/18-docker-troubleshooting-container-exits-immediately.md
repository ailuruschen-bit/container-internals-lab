# Docker Troubleshooting in Practice (1): A Container Stops Right After Starting

**English** | [中文](zh/18-docker-troubleshooting-container-exits-immediately.md) | [日本語](ja/18-docker-troubleshooting-container-exits-immediately.md)

This time I started organizing common Docker troubleshooting scenarios. The first symptom: after running `docker run` or `docker compose up`, the container stops almost as soon as it starts.

When troubleshooting, I do not rush to modify the Dockerfile or rebuild the image. Instead, I collect evidence in the order "state, logs, details."

To reproduce the symptom reliably, this experiment uses a throwaway container that prints an error and returns `1`:

```bash
docker run --name note-exit-lab \
  alpine sh -c 'echo "application startup failed" >&2; exit 1'
```

`--rm` is deliberately not used here, because the exited container has to be kept around so its state and details can be inspected afterward. Clean it up with `docker rm note-exit-lab` once the experiment is over.

## 1. First, Confirm the Container's Actual State

By default, `docker ps` shows only running containers. Exited instances require:

```bash
docker ps -a
```

The experiment shows:

```text
CONTAINER ID   IMAGE    STATUS       NAMES
b70b6baf23b2   alpine   Exited (1)   note-exit-lab
```

This already gives three useful pieces of information:

- The container ID or name, for use in subsequent commands.
- The current state really is `Exited`, rather than restarting repeatedly or still running.
- The `1` in parentheses is the exit code from the main process's most recent exit.

A stopped container does not necessarily mean Docker itself has failed. A container's lifecycle is tied to its main process, so it also stops when the main process finishes normally, in which case the exit code is typically `0`. What really needs to be determined is why it exited, and whether that exit was expected.

## 2. Check the Logs Left by the Main Process

With the ID or name in hand, the next step is to check the logs:

```bash
docker logs <container>
```

`<container>` can be either the container name or its ID. For a container started by Compose, you can also run `docker compose logs <service>` in the directory containing the Compose file, where the argument is the service name under `services:`, such as `app`. `docker compose ps -a` likewise narrows the status list down to the current project.

For example:

```bash
docker logs b70b6baf23b2
```

This experiment produced:

```text
application startup failed
```

When there is a lot of output, start with the last part:

```bash
docker logs --tail 100 <container>
```

`docker logs` mainly retrieves what the container wrote to standard output and standard error. If the application writes its logs only to files inside the container, or fails before it has a chance to output anything, this may be empty. So "no logs" does not mean "no errors."

## 3. Look at the Full `inspect` First, Then Narrow Down the Fields

`inspect` has many fields, and there is no need to memorize every path up front. A more practical approach is to get the container ID with `docker ps -a` first and then look at the full JSON:

```bash
docker inspect <container>
```

The outermost level of the output is an array containing the container's configuration, runtime state, mounts, networking, and other details. Confirm where a field sits in the full structure first, then use `-f` to extract the part you need.

In this scenario, the object most worth checking first is `.State`:

```bash
docker inspect -f '{{json .State}}' <container>
```

The experiment's output looks like:

```json
{"Status":"exited","Running":false,"OOMKilled":false,"ExitCode":1,"Error":"","StartedAt":"...","FinishedAt":"..."}
```

`json` prints the nested `.State` object as valid JSON, which is easier to read than the template's default format.

## 4. What to Focus On in `.State`

| Field | Purpose |
|---|---|
| `Status` | Which state the container is in, such as `running`, `exited`, or `restarting` |
| `ExitCode` | The status code returned by the main process's most recent exit |
| `Error` | An error recorded by the Docker runtime while starting the container |
| `OOMKilled` | Whether the process was killed by the OOM mechanism because of insufficient memory |
| `Restarting` | Whether the container is repeatedly restarting under its restart policy |
| `StartedAt` | When it was most recently started |
| `FinishedAt` | When it most recently stopped |

To narrow down to a single field, access it directly:

```bash
docker inspect -f '{{.State.ExitCode}}' <container>
docker inspect -f '{{.State.OOMKilled}}' <container>
docker inspect -f '{{.State.Error}}' <container>
```

## 5. An Exit Code Only Points in a Direction

A few common values help narrow things down:

| Exit code | Common meaning |
|---|---|
| `0` | The main process finished normally; not necessarily a failure |
| `1` | A general error returned by the application; needs the logs to interpret |
| `126` | The command was found but could not be executed, for example a permission problem |
| `127` | The command to execute could not be found |
| `137` | The process received `SIGKILL`; possibly OOM, possibly a deliberate forced kill |
| `143` | The process received `SIGTERM`; common during a normal stop |

An exit code is not a complete diagnosis. For example, seeing `137` is not enough to conclude that memory ran out; you still need to check:

```bash
docker inspect -f '{{.State.OOMKilled}}' <container>
```

## 6. When the State Is Still Not Enough

If the logs and `.State` only tell me that "startup failed" without saying why, I can go on to check the startup configuration the container actually used:

```bash
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

This confirms the actual entry-point program and default arguments, which helps track down problems such as a mistyped command, a missing file, or startup arguments that are not what was expected.

## 7. The Troubleshooting Order So Far

```text
docker ps -a
    │
    ├── Confirm the container's state, ID, and exit code
    ▼
docker logs <container>
    │
    ├── See what the application wrote to standard output and standard error
    ▼
docker inspect <container>
    │
    ├── Look over the full JSON first
    ▼
docker inspect -f '{{json .State}}' <container>
    │
    └── Depending on the result, go on to ExitCode, OOMKilled, Error, or the startup configuration
```

The point of this process is not to memorize every `inspect` field, but to build a map from the full JSON first and then narrow the scope step by step with `-f`. For "stops right after starting," `.State` is the best first place to look.
