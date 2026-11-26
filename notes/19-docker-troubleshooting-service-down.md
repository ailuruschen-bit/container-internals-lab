# Docker Troubleshooting in Practice (2): The Container Is Running, but the Service Is Down

**English** | [中文](zh/19-docker-troubleshooting-service-down.md) | [日本語](ja/19-docker-troubleshooting-service-down.md)

Last time, the container had exited. This time, the container still shows `Up` in `docker ps`, but the service no longer responds. As long as the container's main process is alive, Docker keeps showing `Up`. What needs to be confirmed next is whether the service process is still there, and what logs it left behind before things went wrong.

## 1. Start with the State and the Most Recent Logs

First, find the container name or ID with `docker ps`. Running `docker compose ps` in the directory containing `compose.yaml` lists only the running containers of the current project; add `-a` to see containers in the same project that have already exited.

| Task | By container name or ID | By Compose service name |
|---|---|---|
| Show the last 20 lines and follow new logs | `docker logs -f --tail=20 <container>` | `docker compose logs -f --tail=20 <service>` |
| Look at the processes directly | `docker top <container>` | `docker compose top <service>` |
| Open an interactive shell | `docker exec -it <container> sh` | `docker compose exec <service> sh` |

If the service has only one container, the commands in either column can usually do the same troubleshooting. For example, `app` is the Compose service name and `05-compose-app-1` is the container name. When a service has multiple replicas, Compose's `logs` and `top` can show several replicas, and `exec` can pick one with `--index`.

I usually look at the last 20 lines first, then retry the failing request once to see whether a new error appears. Pressing `Ctrl+C` stops following the logs; the container keeps running.

## 2. Confirm Whether the Service Process Is Still There

When I only want the process list, I start with `docker top`, which lists the processes from outside the container. If I also need to check configuration files, permissions, or startup arguments, I then enter the container with `docker exec -it <container> sh` and run `ps`. This requires `sh` and `ps` to exist in the image.

If only processes such as the startup script remain and the expected service process is gone, go back to the logs to find out why it exited. If the service process is still there, request the service's listening port from inside the container to narrow down whether the service itself cannot respond or only access from outside the container is failing.

## 3. Write the Fix Back into the Project

Entering the container to change a setting temporarily is fine for testing a hypothesis. But a change that lives only in the current container's writable layer is lost when the container is recreated. Once the cause is confirmed, write the fix back into `compose.yaml`, a configuration file mounted from the host, or the application source; which one to change depends on where the fault came from.

For example, after correcting an environment variable for `app` in `compose.yaml`, run in the project directory:

```bash
docker compose up -d app
docker compose ps
docker compose logs --tail=20 app
```

`up` recreates the container according to the configuration changes. Running `docker compose restart app` on its own only restarts the existing container, which keeps the environment variables and other configuration it was created with. If the fix requires rebuilding the image, use `docker compose up -d --build app`. Finally, send the request that originally failed once more to confirm the service has really recovered.
