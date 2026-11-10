# Getting Started with Docker Compose: From `docker run` to Multi-Service Configuration

**English** | [中文](zh/14-docker-compose-basics.md) | [日本語](ja/14-docker-compose-basics.md)

Today I started learning Docker Compose, and so far I have only done the most basic configuration experiments. This note records my understanding of `compose.yaml`, services, ports and environment variables, and service startup dependencies and health checks.

The most immediate value of Compose is that container configuration otherwise scattered across several `docker run` commands can be kept together in a single YAML file that can be applied again and again.

## 1. What `compose.yaml` Describes

To run a single container on its own, I might write:

```bash
docker run -d \
  --name spring-app \
  -p 127.0.0.1:8081:8080 \
  -e SPRING_PROFILES_ACTIVE=dev \
  spring-lab:v2
```

With Compose, the same information can be written as:

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: dev
```

The correspondence is:

| `docker run` | Compose service configuration |
|---|---|
| `spring-lab:v2` | `image: spring-lab:v2` |
| `--name` | Usually managed by Compose from the project and service names |
| `-p`, `--publish` | `ports` |
| `-e`, `--env` | `environment` |

A Compose file describes the configuration a group of services should run with. Running:

```bash
docker compose up -d
```

makes Compose read `compose.yaml` in the current directory, create the required networks and containers, and start the services in the background.

## 2. How Services Relate to Containers

Each name under `services` represents a service definition:

```yaml
services:
  app:
    image: spring-lab:v2

  cache:
    image: redis:7-alpine
```

`app` and `cache` here are service names. They are not image names, nor are they container names that have to be set by hand.

For now, I think of it like this:

```text
Service
    ├── Which image to use, or how to build it
    ├── How to configure the container's runtime environment
    └── How it relates to other services
              │
              ▼
       Compose creates containers
```

By default, one service creates one container. Compose can also adjust the number of replicas, but a service is the unit of configuration and management, while a container is an actual running instance, so the two are not exactly the same thing.

## 3. Compose Does Not Require the Image to Be Built in Advance

If the file says:

```yaml
services:
  app:
    image: spring-lab:v2
```

Compose uses the existing local image; if there is none locally, it usually tries to get one from a registry according to the pull policy.

Compose can also declare how to build the image directly:

```yaml
services:
  app:
    build:
      context: ../04-java/springboot
      dockerfile: Dockerfile
    image: spring-lab:dev
```

Here `build` tells Compose to build from source, and `image` sets the image name and tag for the result of that build.

### `context` and `dockerfile`

`context` corresponds to the build context at the end of `docker build`:

```text
Directory containing compose.yaml
    │
    └── ../04-java/springboot
            ├── Dockerfile
            ├── pom.xml
            └── src/
```

A relative context path is resolved from the Compose project directory. Here, `../04-java/springboot` is the Spring Boot project directory, and `COPY` in the Dockerfile can only use files that this context makes available.

`dockerfile` specifies the build file, and its relative path is resolved against the build context. If the file is the standard `Dockerfile` at the root of the context, this setting can be omitted; when `context` is not set explicitly, the project directory `.` is used by default.

So these two settings do not always both have to be written out. The benefit of the full form is that the build inputs and the Dockerfile location are explicit.

### `image` Names the Build Result

`build` and `image` are two settings under the same service:

```yaml
build:
  context: ../04-java/springboot
  dockerfile: Dockerfile
image: spring-lab:dev
```

When Compose runs this build, the resulting image is named:

```text
spring-lab:dev
```

Without `image`, Compose can still build, usually using a name derived from the project and service. But that is not a release name I chose explicitly, and when running Compose push, it cannot tell which registry repository to push to. So `image` should be set explicitly whenever the build result needs to be distributed.

The service can be built on its own:

```bash
docker compose build app
```

Or Compose can be told to build before starting:

```bash
docker compose up --build -d
```

When both `build` and `image` are present, whether Compose tries to pull first also depends on `pull_policy` and the specific command. This experiment uses `--build` to explicitly request a build before startup, rather than treating "having `build` means it is always rebuilt automatically" as a fixed rule.

Therefore, "Compose must use an image that has already been built" is not accurate. The more complete statement is:

> Every service ultimately needs an image, but that image can already exist, be pulled from a registry, or be produced by Compose from the `build` configuration.

## 4. `ports`: The Counterpart of `docker run -p`

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
```

The three parts mean the same as with `docker run -p`:

```text
Host address : Host port : Container port
127.0.0.1    :   8081    :      8080
```

`ports` is a list, so a single service can declare several port mappings. Writing the short syntax as a quoted string prevents YAML from parsing certain values that contain colons or special formats as some other type.

Publishing ports is mainly for reaching a service from outside the Compose network. When services in the same Compose project talk to each other, they usually use the other service's name and container port directly, without going through a published host port.

## 5. `environment`: The Counterpart of `docker run -e`

Map syntax:

```yaml
services:
  app:
    image: spring-lab:v2
    environment:
      SPRING_PROFILES_ACTIVE: dev
      LOG_LEVEL: INFO
```

List syntax can also be used:

```yaml
environment:
  - SPRING_PROFILES_ACTIVE=dev
  - LOG_LEVEL=INFO
```

These variables go into the containers Compose creates and override any default `ENV` of the same name in the image, without going back and modifying the image.

Sensitive information such as passwords and tokens should not be committed directly in a Compose file. I still need to learn about `.env`, `env_file`, secrets, and other configuration mechanisms, and which problem each of them solves.

## 6. `volumes`: Declaring Storage and Mounting It into a Service

This time the PostgreSQL service gained a named volume:

```yaml
services:
  db:
    image: postgres:17-alpine
    volumes:
      - pgdata:/var/lib/postgresql/data

volumes:
  pgdata:
```

`volumes` appears in two places here, with different roles.

### Top-Level `volumes` Declares Named Volumes

```yaml
volumes:
  pgdata:
```

This tells Compose that the current project has a named volume with the logical name `pgdata`. An empty configuration means the Docker Engine's default volume driver is used.

Declaring it in the file does not immediately create the actual resource. When `docker compose up` runs, Compose creates it if needed and reuses it if it already exists.

By default, Compose prefixes the actual resource name with the project name. In this experiment,

```bash
docker volume ls
```

shows:

```text
05-compose_pgdata
```

`pgdata` is the logical name in the Compose file, and `05-compose_pgdata` is the volume name actually visible in the Docker Engine. When needed, you can also run:

```bash
docker volume inspect 05-compose_pgdata
```

### `volumes` Inside a Service Specifies the Mount

```yaml
services:
  db:
    volumes:
      - pgdata:/var/lib/postgresql/data
```

The short syntax is split by a colon into:

```text
Volume logical name : Mount path inside the container
pgdata              : /var/lib/postgresql/data
```

The top-level declaration only defines which named volumes are available; it does not automatically grant any service access to them. It is the service-level configuration that mounts `pgdata` at the location where PostgreSQL actually stores its database files.

### Why the Data Survives Container Removal

The database files are no longer written only to the `db` container's writable layer, but to a separate volume:

```text
05-compose-db-1
    │ /var/lib/postgresql/data
    ▼
05-compose_pgdata
```

After `docker compose down`, the containers and the default network are removed, but named volumes are kept by default. In this experiment, the Compose containers had already been stopped and removed, yet `05-compose_pgdata` could still be seen with `docker volume ls`.

The next `docker compose up` recreates the `db` container and mounts the original volume, so the database can keep reading the data written earlier.

That is the basic meaning of data persistence: the data's lifecycle is no longer tied entirely to a single container.

Persisting data in a volume, however, does not mean database backup, cross-host replication, or version upgrades have been taken care of. The following command also actively removes the volumes managed by Compose:

```bash
docker compose down -v
```

Those may contain database data, so `-v` must not be used casually as an ordinary cleanup flag.

## 7. Compose Creates a Service Network by Default

When there are multiple services, Compose creates a network for the current project by default and attaches every service that does not specify another network.

Containers on the same network can resolve each other by service name:

```text
app  ──→  cache:6379
```

The stable service name `cache` should be used here, rather than remembering a container IP that was assigned dynamically at some point.

This is also where Compose is more than "a few run commands written as YAML": it manages the networking and lifecycle of containers as a single project.

### What Compose Creates Are Still Docker Resources

Compose adds a management abstraction of services and projects, but the resources are ultimately still created and managed by the same Docker Engine, so running:

```bash
docker ps
```

shows the actual containers behind the services:

```text
05-compose-app-1
05-compose-db-1
```

`docker compose ps` looks only at the current Compose project, whereas `docker ps` looks at all running containers in the current Docker Engine.

The default network can also be observed with ordinary Docker commands:

```text
docker network ls
    └── 05-compose_default
```

Here `05-compose` is the project name and `default` is the default bridge network Compose created automatically. To see the connection details, use `docker network inspect 05-compose_default`.

```text
Compose service definition  ──→  Docker container
Compose default network     ──→  Docker bridge network
```

A service and a container are still not exactly the same thing, because one service can have several replicas; each actual replica is a container visible in the Docker Engine.

## 8. What `depends_on` Guarantees with Only a Service Name

The short form:

```yaml
services:
  app:
    image: spring-lab:v2
    depends_on:
      - cache

  cache:
    image: redis:7-alpine
```

Compose creates and starts `cache` first, then starts `app`, and shuts them down in the reverse dependency order.

But "the container has started" does not mean "the application inside is ready to accept requests." For example, a database process may already be running while still initializing, and the short form of `depends_on` does not wait for it to become truly usable.

So `depends_on` addresses dependency order; it does not automatically understand an application's actual readiness.

## 9. `healthcheck`: Defining What Healthy Means

The correct key is `healthcheck`, not `health_check`:

```yaml
services:
  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

This means Docker periodically runs the following inside the `cache` container:

```text
redis-cli ping
```

When the command succeeds, the container can enter the `healthy` state. The fields do the following:

- `test`: the check command that is actually run.
- `interval`: the time between two checks.
- `timeout`: the maximum time allowed for a single check.
- `retries`: how many consecutive failures mark the container as unhealthy.
- `start_period`: time reserved for the application to start; failures during this period do not immediately count toward the retries.

The check command runs inside the target container, so the image must contain `redis-cli`, `curl`, or whatever other program the check needs. Having a command installed on the host does not mean it is available inside the container.

A health check only produces a health status; a container becoming `unhealthy` does not automatically fix the application. Whether to restart, replace, or alert depends on the runtime and orchestration mechanisms around it.

## 10. Making a Dependency Wait for a Healthy Status

To make `app` wait until `cache` is actually healthy, use the long form of `depends_on`:

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: compose
    depends_on:
      cache:
        condition: service_healthy

  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

The startup sequence becomes:

```text
Create cache
    ↓
Start cache
    ↓
healthcheck passes
    ↓
Create and start app
```

This example only illustrates Compose's dependency and health mechanisms. Whether `spring-lab:v2` actually uses Redis depends on the Spring Boot project's own dependencies and configuration; declaring `depends_on` in Compose does not give the application Redis functionality by itself.

## 11. The Most Basic Compose Operations

In the directory containing `compose.yaml`:

```bash
docker compose config
```

This parses and prints the final configuration first, which is useful for catching YAML or configuration-merging problems before startup.

Create and start the services:

```bash
docker compose up -d
```

Check status and logs:

```bash
docker compose ps
docker compose logs -f
```

Stop and remove the containers and default network created by the current project:

```bash
docker compose down
```

By default, `down` removes neither images nor named volumes. `docker compose down -v` additionally removes the volumes managed by Compose, which may hold persistent data such as databases, so it should not be treated as a risk-free everyday cleanup command.

## 12. The Understanding So Far

For now, I think of Compose as these layers:

```text
Service build and runtime configuration
    ├── build / image
    ├── ports / environment
    └── volumes mounts

Relationships between services
    ├── Default network and name resolution
    ├── depends_on
    └── healthcheck

Independent resources
    ├── Image
    ├── Network
    └── Named Volume
```

`compose.yaml` is not a word-for-word translation of shell commands into YAML. It declares the desired running state of a group of services. Compose then creates images, networks, and containers from that configuration and manages their startup and shutdown together.

That said, what Compose mainly solves for now is defining and managing the lifecycle of a multi-container application within a single Docker environment. It has some orchestration capabilities, but it cannot be equated with a cluster orchestration system that handles scheduling, self-healing, and rolling releases across multiple servers.

This is still only the beginning of learning Compose. Next, I need to understand `.env`, configuration overrides, recreation behavior, and how a complete application stack is actually organized.
