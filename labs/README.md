# Labs

The hands-on files behind the notes. Each directory is a self-contained build context: change into it and run the commands in the comments at the top of its `Dockerfile` or `docker-compose.yaml`.

The labs were run on macOS with OrbStack; any Docker Engine on Linux works the same way.

| Lab | What it shows | Note |
|---|---|---|
| [`docker-fs-lab/`](docker-fs-lab/) | A directory shared with a container through a bind mount | [06](../notes/06-docker-mount-and-network-practice.md) |
| [`docker-image-lab/01-hello/`](docker-image-lab/01-hello/) | `FROM`, `WORKDIR`, `COPY`, `CMD`, and the build context | [09](../notes/09-docker-image-build-hello-world.md) |
| [`docker-image-lab/02-nginx/`](docker-image-lab/02-nginx/) | Inheriting `ENTRYPOINT`/`CMD` and replacing a file in an upper layer | [10](../notes/10-docker-image-build-nginx.md) |
| [`docker-image-lab/03-cache/`](docker-image-lab/03-cache/) | Build cache hits and invalidation by instruction order | [11](../notes/11-docker-image-build-cache-args-ignore.md) |
| [`docker-image-lab/03-env/`](docker-image-lab/03-env/) | Build-time `ARG` vs. runtime `ENV` | [11](../notes/11-docker-image-build-cache-args-ignore.md) |
| [`docker-image-lab/03-ignore/`](docker-image-lab/03-ignore/) | Filtering the build context with `.dockerignore` | [11](../notes/11-docker-image-build-cache-args-ignore.md) |
| [`docker-image-lab/04-java/simple/`](docker-image-lab/04-java/simple/) | Single-stage Java build on a JDK image | [12](../notes/12-docker-image-build-java-multistage.md) |
| [`docker-image-lab/04-java/multi-stage/`](docker-image-lab/04-java/multi-stage/) | Multi-stage build: compile on a JDK, run on a JRE | [12](../notes/12-docker-image-build-java-multistage.md) |
| [`docker-image-lab/04-java/entrypoint/`](docker-image-lab/04-java/entrypoint/) | How `ENTRYPOINT`, `CMD`, and run-time arguments combine | [12](../notes/12-docker-image-build-java-multistage.md) |
| [`docker-image-lab/04-java/springboot/`](docker-image-lab/04-java/springboot/) | Spring Boot image with a Maven build stage and a non-root runtime | [12](../notes/12-docker-image-build-java-multistage.md), [13](../notes/13-docker-image-version-rollback-registry.md), [15](../notes/15-docker-user-best-practices.md) |
| [`docker-image-lab/05-compose/`](docker-image-lab/05-compose/) | The Spring Boot app and PostgreSQL as one Compose project | [14](../notes/14-docker-compose-basics.md), [16](../notes/zh/16-docker-compose-env-and-secrets.md) |

## Bind mount lab

`docker-fs-lab/` is the directory mounted into the container:

```bash
docker run --rm -it \
  --mount type=bind,source="$PWD/docker-fs-lab",target=/data \
  alpine sh
```

Inside the container, `cat /data/test.txt` reads the host file, and anything written under `/data` appears in `docker-fs-lab/` on the host.

## Notes on the Compose lab

- The Compose project name comes from the directory name, so resources are named `05-compose-app-1`, `05-compose_pgdata`, and so on. Keep the directory name if you want them to match the note.
- Copy `.env.example` to `.env` and `secrets/db_password.txt.example` to `secrets/db_password.txt` before starting the project. Both local files are ignored by Git.
