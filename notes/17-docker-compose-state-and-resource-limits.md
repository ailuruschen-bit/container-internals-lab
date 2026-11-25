# Docker Compose: Observing State, Restarts, and Resource Limits

**English** | [中文](zh/17-docker-compose-state-and-resource-limits.md) | [日本語](ja/17-docker-compose-state-and-resource-limits.md)

Once I understood the basic Compose configuration, I turned to another question: after containers are created, how do I observe whether they are running properly, and how do I limit their use of host resources?

## 1. Observing State from the Project Down to Containers

List the Compose projects running in the current Docker Engine:

```bash
docker compose ls
```

In this experiment, the output was:

```text
NAME         STATUS       CONFIG FILES
05-compose   running(2)   .../05-compose/docker-compose.yaml
```

`running(2)` means this Compose project currently has two running containers. It is a project-level summary and says nothing about whether the service inside each container is actually usable.

Next, look at the actual containers:

```bash
docker ps
```

```text
05-compose-app-1   Up 8 days
05-compose-db-1    Up 8 days (healthy)
```

This shows the containers that the Compose services ultimately created, along with their uptime and health status.

The commands observe different scopes:

```text
docker compose ls      Compose projects in the current Engine
docker compose ps      Containers of the current Compose project
docker ps              All running containers in the current Engine
docker ps -a           All containers in the current Engine, including stopped ones
```

## 2. `restart`: Basic Recovery After a Container Exits

Compose can set a restart policy for a service:

```yaml
services:
  app:
    image: spring-lab:dev
    restart: unless-stopped
```

Common policies include:

| Setting | Meaning |
|---|---|
| `no` | The default; never restart automatically |
| `on-failure` | Restart when the main process exits with a non-zero status |
| `always` | Keep trying to restart after the container stops |
| `unless-stopped` | Restart automatically, but not after being explicitly stopped or removed |

The one I use most at the moment is `unless-stopped`. It suits services that need to run long term while still respecting the intent of a manual stop.

However, `restart` is concerned with **whether the container has stopped**. As long as the container's main process is still running, the restart policy is not triggered just because the service inside can no longer respond properly.

## 3. `healthcheck`: Checking Whether the Service Inside Is Usable

`healthcheck` periodically runs a check command inside the container:

```yaml
services:
  db:
    image: postgres:17-alpine
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U $$POSTGRES_USER -d $$POSTGRES_DB"]
      interval: 5s
      timeout: 5s
      retries: 10
```

Docker maintains the container's health status based on the results:

```text
starting ──→ healthy
     │
     └────→ unhealthy
```

The check command runs inside the target container, so the image must contain `pg_isready`, `curl`, or whatever other program it calls.

On the dependent side:

```yaml
services:
  app:
    depends_on:
      db:
        condition: service_healthy
```

On first startup, Compose waits for `db` to pass its health check before starting `app`.

These need to be kept clearly apart:

```text
restart                         Whether a container is restarted after it exits
healthcheck                     Whether a running container is healthy
depends_on + service_healthy    Whether a dependent waits for health before its first start
```

If a container becomes `unhealthy` while running, Docker does not restart it automatically because of the health status alone, and `depends_on` does not rerun the startup orchestration at that point either. How to recover from this kind of runtime failure is something I will come back to in a later full-project experiment, with concrete scenarios.

## 4. Limiting CPU and Memory with Cgroups

By default, a container has no CPU or memory limits and may compete heavily for resources with other processes on the host. Compose can hand resource constraints to Docker, and Linux cgroups ultimately enforce them:

```yaml
services:
  app:
    cpus: 1.0
    mem_limit: 512m

  db:
    cpus: 0.5
    mem_limit: 256m
```

`cpus` is the CPU quota the container is allowed to use:

```text
cpus: 0.5   at most roughly half a logical CPU's worth of compute time
cpus: 1.0   at most roughly one logical CPU's worth of compute time
```

It limits the total CPU time available over a period; it does not pin the container to a particular CPU core. Binding to specific cores is a separate setting, `cpuset`.

`mem_limit` is the upper bound on the memory the container can allocate. Beyond the hard limit, memory allocations may fail and the process may be killed by the OOM mechanism. Only if the container exits as a result can the restart policy above play a part in recovery.

The purpose of resource limits is not only to protect the host but also to make an application's resource needs explicit before deployment. Limits set too low create instability of their own, so they need to be tuned gradually against actual monitoring.

## 5. Observing Resource Usage with `docker stats`

Continuously observe all running containers:

```bash
docker stats
```

Observe only a specific container:

```bash
docker stats 05-compose-app-1
```

The command refreshes continuously by default; press `Ctrl+C` to exit. For a single snapshot of the current values:

```bash
docker stats 05-compose-app-1 --no-stream
```

The main fields are:

| Field | Meaning |
|---|---|
| `CPU %` | Current CPU utilization |
| `MEM USAGE / LIMIT` | Current memory usage and the limit |
| `MEM %` | Memory used as a percentage of the limit |
| `NET I/O` | Network traffic sent and received |
| `BLOCK I/O` | Block device reads and writes |
| `PIDS` | Number of processes and threads in the container |

In this experiment, before `mem_limit` was configured, both containers showed a memory limit close to the memory available to Docker's Linux environment. Only after the containers were recreated with limits did LIMIT in `MEM USAGE / LIMIT` change to the values specified in Compose.

After modifying `compose.yaml`, run:

```bash
docker compose up -d
docker stats --no-stream
```

Compose recreates the affected containers according to the configuration changes, and the data in named volumes is kept.

## 6. The Understanding So Far

```text
Compose project state
    └── docker compose ls

Container runtime state
    ├── docker compose ps / docker ps
    ├── restart handles process exits
    └── healthcheck describes service health

Resource boundaries
    ├── cpus / mem_limit are written as cgroup constraints
    └── docker stats shows actual usage
```

`restart`, `healthcheck`, and resource limits address three different problems: recovery after a container exits, observing whether a service is available, and protecting host resources. Together they provide the most basic state management, but they are not yet a complete failure detection and automatic recovery system.
