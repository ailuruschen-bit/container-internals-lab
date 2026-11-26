# Docker Troubleshooting in Practice (3): Spring Boot Cannot Resolve the Database Hostname

**English** | [中文](zh/20-docker-troubleshooting-db-name-resolution.md) | [日本語](ja/20-docker-troubleshooting-db-name-resolution.md)

When Spring Boot cannot connect to PostgreSQL, I still start the way I would with any Java application: by reading the exception chain. Once I see `UnknownHostException: db`, I confirm from the application's own network environment whether `db` can be resolved.

## 1. Find the Hostname That Failed to Resolve in the Logs

```bash
docker logs --tail=100 <container>
docker compose logs --tail=100 app
```

Look at the last 100 lines first and follow the `Caused by` entries down to the root cause. For example, if the JDBC URL is `jdbc:postgresql://db:5432/labdb` and the logs contain `UnknownHostException: db`, check name resolution for `db` first. If the logs are long and only part of the exception chain is shown, increase `--tail`.

## 2. Test Resolution from the Application Container

If `app` is still running, run `docker compose exec app getent hosts db` directly in the original container. If it has already exited, start a temporary container from the same service definition in the directory containing the Compose file:

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`run` creates a new container from the `app` configuration, `--entrypoint sh` drops it into a shell, and `--rm` removes it automatically on exit. The original container is kept, so its logs can still be examined. If `db` is already running, add `--no-deps` while debugging to skip starting the dependent services.

One Linux command worth noting here: `getent hosts db`. It looks up a hostname according to the container's own system configuration, so the result may come from DNS or from `/etc/hosts`. The subcommand is the plural `hosts`. Confirm that the image has `getent` before drawing conclusions from the result.

## 3. Narrow Down Based on the Result

In the temporary container in this experiment, `getent hosts db` returned an IP address, while a lookup for a nonexistent name returned no record.

- If `db` cannot be resolved, first check the database container's state with `docker compose ps -a`, then verify that the hostname in the JDBC URL matches the Compose service name and that `app` and `db` share a network.
- If the address can be resolved, move on to checking PostgreSQL's port `5432` and authentication. If the original Java process still reports `UnknownHostException`, also check the hostname it actually uses. The lookup in the temporary container only reflects how the system resolved names at that moment.

In this experiment, the service name `db` is the one to use. The IP address may change when the container is recreated, so keep using the service name when correcting the JDBC URL.
