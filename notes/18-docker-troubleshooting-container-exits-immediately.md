# Docker Troubleshooting in Practice

**English** | [中文](zh/18-docker-troubleshooting-container-exits-immediately.md) | [日本語](ja/18-docker-troubleshooting-container-exits-immediately.md)

When a container fails, I start with `ps`, then check logs and `inspect`. These five cases start the same way. The symptom determines what I check next.

## Start with status and logs

```bash
docker ps -a
docker logs --tail=100 <container>
docker compose logs --tail=100 <service>
```

In the Compose file directory, `docker compose ps -a` shows containers in the current project. Plain Docker commands take a container name or ID. `docker compose logs`, `exec`, and `top` take a service name.

## 1. The container exits immediately

A container exits with its main process. `Exited (0)` can be a one-off job completing normally. For an unexpected exit, I look at `ExitCode`, `Error`, and the timestamps in `.State`, then check `.Config.Entrypoint` and `.Config.Cmd`. A failure during startup may appear in `Error` before the application writes any logs.

```bash
docker inspect -f '{{json .State}}' <container>
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

If I need to find a field, I start with the full `docker inspect <container>` output. It is a JSON array, so `.[0]` selects the first container when using `jq`.

## 2. The container is up but the service is down

```bash
docker logs -f --tail=20 <container>
docker top <container>
docker exec -it <container> sh
```

When the container is `Up`, I follow recent logs and use `top` to see whether the service process is still there. For files, permissions, or listening ports, I open a shell with `exec` and use tools available in the image, such as `ps`. If the service process is present, testing its port from inside the container helps narrow down where the request fails.

Once a fix works, apply it in the project. Run `docker compose up -d app` after editing `compose.yaml`. `docker compose restart app` reuses the existing container configuration. If the fix requires rebuilding the image, use `docker compose up -d --build app`, then retry the failed request.

## 3. Spring Boot cannot resolve the database hostname

When Spring Boot cannot connect to PostgreSQL, I follow `Caused by` in the logs. For `UnknownHostException: db`, I run `getent hosts db` in the application container. If it has exited, I open a temporary shell with the same service configuration.

```bash
docker compose exec app getent hosts db
```

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`getent hosts` uses the system name lookup configuration, which can include `/etc/hosts`. If `db` does not resolve, check the JDBC URL, database container status, and shared networks. If it resolves, check the database port and authentication.

In the lab, the temporary container resolved `db` and returned no record for a nonexistent name.

## 4. The health check fails

```bash
docker inspect -f '{{json .State.Health}}' <container>
docker inspect <container> | jq '.[0].State.Health'
docker inspect <container> | jq '.[0].State.Health.Log[-1]'
```

For `Up (unhealthy)`, I check `.State.Health`. `FailingStreak` counts consecutive failures and `Log[-1]` selects the latest result. I look at `ExitCode` and `Output` first. To rerun the probe, I find the configured command in `.Config.Healthcheck`.

In the lab, a missing file caused the probe to fail and `Output` contained `ready file missing`.

## 5. The container is killed due to memory pressure

A sudden `Exited (137)` with no obvious application error points me toward OOM. `137` usually corresponds to `SIGKILL`, which can also come from a manual kill. `OOMKilled: true` means Docker recorded an OOM kill. `Error` may still be empty.

```bash
docker inspect <container> | jq '.[0].State | {Status, ExitCode, OOMKilled, Error}'
docker inspect <container> | jq '.[0].HostConfig.Memory'
```

A temporary container limited to `32 MiB` reproduced `ExitCode: 137` with `OOMKilled: true`.

`.HostConfig.Memory` is in bytes. A value of `0` indicates no explicit container memory limit. Compare it with Compose `mem_limit`. If the problem is reproducible, watch `MEM USAGE / LIMIT` in `docker stats <container>` while the container is running to distinguish a spike from steady growth.
