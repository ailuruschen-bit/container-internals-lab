# Docker `USER`: Running Applications as a Non-root User

**English** | [中文](zh/15-docker-user-best-practices.md) | [日本語](ja/15-docker-user-best-practices.md)

Today I studied the security practice behind the Dockerfile `USER` instruction. The problem it addresses is straightforward: if an application does not need administrator privileges to run normally, the container's main process should not run as root by default.

That said, "root in a container equals root on the host" is not accurate. A container is still constrained by mechanisms such as namespaces, capabilities, seccomp, and the scope of its mounts. The point of using a non-root user is to further narrow what an attacker can do if the application is compromised, adding another layer of defense in depth to the container.

## 1. What Happens Without `USER`

If neither the base image nor the Dockerfile specifies another user, the container process usually runs as root by default.

Check the Spring Boot image built earlier:

```bash
docker image inspect spring-lab:v2 \
  -f '{{json .Config.User}}'
```

On my machine, the result was an empty string:

```text
""
```

This means the image does not explicitly set a default user, so root is used at run time. You can also verify it directly:

```bash
docker run --rm \
  --entrypoint id \
  spring-lab:v2
```

If the output contains `uid=0(root)`, the container process is running as root.

## 2. Root in a Container Does Not Instantly Mean Full Control of the Host

Even with UID 0, Docker does not hand all of Linux root's capabilities to the container by default, and the filesystem and process views the container sees are isolated. So when an application vulnerability is exploited, the attacker does not automatically become root on the host just because the process is root inside the container.

Root inside a container still has much greater destructive power, however. For example, it can:

- Read or modify configuration and files inside the container that only root was meant to access.
- More easily tamper with the application process, start additional programs, or change file permissions.
- Do more damage to directories mounted into the container.
- Amplify the risk when extra capabilities, privileged mode, or kernel vulnerabilities are present.

If the container also mounts the Docker socket or sensitive host paths, or runs with high-privilege settings such as `--privileged`, the risk rises further.

So the reason to use a non-root user is not that it can replace all container isolation, but that it follows the principle of least privilege: the application gets only the permissions it actually needs to do its job.

## 3. Which Stages `USER` Actually Affects

The basic syntax is:

```dockerfile
USER <user>
USER <user>:<group>
USER <UID>:<GID>
```

`USER` sets the default user for the subsequent instructions of the current stage, including:

- Subsequent `RUN` build steps.
- The `ENTRYPOINT` at container run time.
- The `CMD` at container run time.

For example:

```dockerfile
USER app:app

RUN whoami
ENTRYPOINT ["java", "-jar", "app.jar"]
```

Here, `RUN whoami` already runs as `app` during the build, and the final Java process also runs as `app` by default.

So `USER` cannot be placed casually at the top of a Dockerfile. Installing software, creating system directories, or modifying root-owned files after switching usually fails for lack of permission.

## 4. Recommended Order: Do the Privileged Work First, Drop Privileges Last

A clearer Dockerfile order is:

```text
Start the build stage as root
    ↓
Install software, create the user and group
    ↓
Create directories and set ownership
    ↓
Copy the application files
    ↓
USER switches to an unprivileged user
    ↓
ENTRYPOINT / CMD starts the application
```

The idea is not to switch to an unprivileged user first and then keep switching back to root whenever privileges are needed. Privileged operations that can be grouped earlier should be done together as far as possible, and the runtime identity at the end of the Dockerfile should remain non-root.

When a later build step really does need a temporary switch, write it out explicitly:

```dockerfile
USER root
RUN some-privileged-build-step
USER app:app
```

But it is the last `USER` in effect in the final stage that determines the container's default runtime identity, so you must not switch back to root and forget to drop privileges again.

## 5. Creating the User in the Java Runtime Stage

For the earlier Maven multi-stage build, the user should be created in the final JRE stage:

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build

WORKDIR /build
COPY pom.xml .
RUN mvn dependency:go-offline
COPY src ./src
RUN mvn clean package -DskipTests


FROM eclipse-temurin:21-jre

RUN groupadd --gid 10001 app && \
    useradd --uid 10001 \
            --gid 10001 \
            --no-create-home \
            app

WORKDIR /app

COPY --from=build \
     --chown=10001:10001 \
     /build/target/*.jar \
     app.jar

USER 10001:10001

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
```

A few key points:

1. `groupadd` and `useradd` run as root.
2. The user is created in the final JRE stage, not only in the Maven builder stage.
3. `COPY --chown` sets the file owner directly while copying the JAR.
4. `USER` comes after the steps that need root privileges.
5. The Java process ultimately runs as UID/GID `10001:10001`.

The commands for creating users can differ between base images. For example, Alpine commonly uses `addgroup` and `adduser`, while Debian- and Ubuntu-based images commonly use `groupadd` and `useradd`, so the commands cannot be copied mechanically to every base image.

## 6. Why Specify the UID and GID Explicitly

If the system is left to assign IDs automatically, the UID/GID may differ between build results. This hardly matters while the container only accesses files inside the image, but once a volume or host directory is mounted, the kernel checks permissions using the numeric UID/GID.

Explicitly using, for example:

```text
UID 10001
GID 10001
```

keeps file ownership more stable and makes it easier to align with storage permissions in the deployment environment.

There is no universal rule for the exact numbers, but they should be a UID/GID not already taken in the base image, and the convention should be kept consistent across the team and deployment environments.

`USER app:app` is easier to read, while `USER 10001:10001` does not depend on resolving the user by name when the container runs. Either works; what matters is that the user, group, and file permissions in the image are consistent.

## 7. What `user:group` Means

```dockerfile
USER app:app
```

The part before the colon is the user, and the part after it is the default group. Numeric IDs can also be used:

```dockerfile
USER 10001:10001
```

When a group is specified explicitly, the process uses that group, and the Dockerfile `USER` instruction does not automatically keep any other supplementary groups configured for that user. If the application really needs additional group permissions, handle them explicitly in the runtime configuration rather than assuming that every supplementary group listed in `/etc/group` will take effect automatically.

## 8. The Non-root User Must Actually Have the Permissions It Needs

Adding a single `USER app` line does not automatically fix file permissions. If the application needs to read the JAR, write logs, or create temporary files, the corresponding directories must allow that user access.

For example, if the application needs to write to `/app/data`:

```dockerfile
RUN mkdir -p /app/data && \
    chown -R 10001:10001 /app/data

USER 10001:10001
```

When a volume or bind mount is attached, the mounted content replaces the image's original view of that path, and its UID/GID comes from the actual storage. Even if the Dockerfile already ran `chown`, a directory mounted at run time can still produce `Permission denied` because of mismatched ownership.

So the common problems after switching to non-root are often not in the program itself, but:

- The application directory still belongs to root.
- Log, cache, or upload directories are not writable.
- The UID/GID of the mounted storage does not match the container user.
- The program depends on operations only root can perform.

The right fix is to grant only the directory permissions the application needs, not to switch the whole container back to root just to get around the error.

## 9. `USER` Can Still Be Overridden at Run Time

The Dockerfile `USER` is a secure default, not an enforced policy that cannot be overridden.

`docker run` can specify:

```bash
docker run --rm \
  --user 10001:10001 \
  my-app:v1
```

The Compose equivalent:

```yaml
services:
  app:
    image: my-app:v1
    user: "10001:10001"
```

Whoever runs the container may also override the image's default explicitly with `--user 0`. The value of the Dockerfile `USER` is therefore to make the secure way the default behavior; stricter restrictions also require deployment platform policies, capability controls, read-only filesystems, and avoiding dangerous mounts.

## 10. The Understanding So Far

My current understanding of `USER` can be summarized as:

```text
root only prepares the runtime environment
    ├── Installs dependencies
    ├── Creates the user and group
    ├── Creates directories
    └── Configures file permissions
            │
            ▼
USER app:app
            │
            ▼
An unprivileged user runs the application
```

Using a non-root user cannot guarantee that a container is absolutely secure, and root inside a container cannot simply be equated with root on the host. What it really addresses is this: when the application itself is exploited, the attacker first gets only a process identity with limited permissions, rather than a root that can access every file in the container and perform every privileged operation the container allows.

The most practical principle: **if the application does not need root, explicitly create a user in the final runtime stage, prepare the directory permissions it needs, and make `USER` one of the last settings before the application starts.**
