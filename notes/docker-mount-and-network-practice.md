# Hands-On Docker Basics: Mapping Mounts and Networks to Kernel Mechanisms

**English** | [中文](zh/docker-mount-and-network-practice.md) | [日本語](ja/docker-mount-and-network-practice.md)

When I studied namespaces, cgroups, OverlayFS, and container networking, I was mostly dealing with abstract mechanisms. The goal of this lab was not to memorize Docker commands, but to connect those mechanisms to container behavior I could actually observe.

My lab environment is **macOS + OrbStack**. The Docker Engine actually runs inside a Linux environment provided by OrbStack, so some paths involve an extra layer of file sharing or port forwarding compared with native Docker on Linux.

## 1. Understand the Runtime Environment First

The current environment can be simplified as follows:

```text
macOS
  └── Docker CLI
        └── Docker Context: orbstack
              └── Docker socket forwarded to macOS
                    └── Docker Engine in the OrbStack VM
                          └── Linux containers
```

The Docker CLI is only a client; a Docker context records which engine the client should connect to. An OrbStack Linux machine and a Docker container are also two different kinds of runtime environments, and there is no need to enter the Linux machine first for day-to-day container work.

I used the following commands to check the environment:

```bash
orb start
docker context ls
docker --context orbstack info
docker version
```

At one point I ran into this error:

```text
failed to connect to the docker API
... docker.sock: no such file or directory
```

This does not mean the Docker command is missing; it means the socket the CLI selected is unavailable. With OrbStack, the usual causes are that OrbStack is not running or that the current context points to the wrong location.

## 2. Understanding Container Processes Through `run` and `exec`

Many Docker commands can be read as "object to manage + action":

```bash
docker network create lab-net
docker network inspect lab-net
docker volume ls
```

There are also common shorthands; for example, `docker ps` is equivalent to `docker container ls`.

Creating a container and entering an existing one are two different things:

```bash
docker run -dit --name process-lab alpine sh
docker exec -it process-lab sh
docker stop process-lab
docker rm process-lab
```

- `run` creates a new container runtime environment and starts its main process.
- `exec` starts an additional process inside a running container's environment.
- Exiting a shell started with `exec` does not end the container's main process.
- When the main process exits, the container normally stops as well.

Here, `alpine` is a lightweight Linux userspace image, and `sh` is the program executed when the container starts. The image contains programs, shared libraries, and configuration files, but no Linux kernel of its own; the container still uses the kernel of the OrbStack Linux environment.

The options I used most in this lab were:

| Option | Stands for | Effect |
|---|---|---|
| `-i` | interactive | Keeps standard input open |
| `-t` | tty | Allocates a pseudo-terminal |
| `-d` | detach | Runs in the background |
| `--rm` | remove | Removes the container automatically when it exits |
| `--name` | name | Assigns a container name |
| `-u` | user | Specifies the user to run as |
| `-p` | publish | Publishes a container port |
| `--mount` | mount | Configures a filesystem mount |
| `--network` | network | Specifies a Docker network |

Short options can be combined, as in `-it` and `-dit`. Here `i` stands for interactive, `t` for TTY, `d` for detach, and `p` for publish.

## 3. Bind Mounts: Sharing Files Between a Container and macOS

First, I prepared a directory and a file on macOS:

```bash
mkdir -p "$HOME/docker-fs-lab"
echo "Hello from macOS" > "$HOME/docker-fs-lab/test.txt"
```

Then I mounted that directory at `/data` in a container:

```bash
docker run --rm -it \
  --name mount-lab \
  --mount type=bind,source="$HOME/docker-fs-lab",target=/data \
  alpine sh
```

The three fields in `--mount` mean:

- `type=bind`: use a bind mount.
- `source`: the mount source, here a directory on macOS.
- `target`: the path through which the container accesses it.

Inside the container, I read one file and wrote another:

```bash
cat /data/test.txt
echo "Hello from container" > /data/container.txt
```

After exiting the container, the new file is still visible on macOS:

```bash
cat "$HOME/docker-fs-lab/container.txt"
```

This experiment shows that `/data` is not a copy of the data placed inside the container, but a path through which the container reaches a shared resource. In OrbStack, macOS files are first exposed to the Linux environment through a file-sharing mechanism and then mounted into the container.

### Volumes and tmpfs

Besides bind mounts, Docker commonly uses two other types of mounts:

| Type | Data source | Typical use |
|---|---|---|
| `bind` | A specified host path | Development directories, configuration files |
| `volume` | Docker-managed storage | Databases, persistent data |
| `tmpfs` | A memory-backed temporary filesystem | Caches, temporary data |

A named volume:

```bash
docker run --rm -it \
  --mount type=volume,source=mydata,target=/data \
  alpine sh

docker volume ls
docker volume inspect mydata
```

A named volume can survive the removal of the containers that use it. Unlike a bind mount, its actual storage location is managed by Docker.

tmpfs:

```bash
docker run --rm -it \
  --mount type=tmpfs,target=/data \
  alpine sh
```

A tmpfs mount needs no `source`, and its data is not kept as ordinary persistent data once the container stops.

### Observing the Container's Root Filesystem

Inside the container, I ran:

```bash
mount | grep ' on / '
mount | grep ' on /proc '
```

In my lab, the output looked like this:

```text
overlay on / type overlay (...)
proc on /proc type proc (...)
```

This shows that the container's `/` is assembled by OverlayFS, while `/proc` is a separately mounted procfs. OverlayFS typically uses `lowerdir` for the base layers, `upperdir` to store the current container's changes, and `workdir` for its internal operations.

The root directory a container sees is therefore not a dedicated physical disk but a view composed of multiple filesystems and mount relationships. Which storage driver or snapshot mechanism is actually used still depends on the Docker environment, so the `overlay` output from one experiment should not be taken as a fixed result for every environment.

## 4. Custom Bridges: Creating a Virtual LAN for Containers

Docker provides several networks by default:

```bash
docker network ls
```

| Network | Driver | Purpose |
|---|---|---|
| `bridge` | bridge | The default virtual bridge network |
| `host` | host | Shares the network namespace of the Docker host environment |
| `none` | null | Configures no regular external network connectivity |

On native Linux, `host` refers to the Docker host's network. OrbStack adds macOS integration on top of these semantics so that services using host networking can also be reached through the Mac's `localhost`. Observations from this lab therefore cannot simply be assumed to hold in every Docker environment.

I created a custom bridge network:

```bash
docker network create lab-net
docker network inspect lab-net
```

The information I got in the lab included:

```text
Name: lab-net
Driver: bridge
Subnet: 192.168.97.0/24
Gateway: 192.168.97.1
```

IPAM here stands for IP Address Management: Docker manages the subnet and assigns internal IP addresses to the containers attached to the network.

I created two containers and attached them to this network directly:

```bash
docker run -dit --name net-a --network lab-net alpine sh
docker run -dit --name net-b --network lab-net alpine sh
```

The underlying structure can be simplified as:

```text
Linux Kernel
      │
   Bridge
    /    \
  veth  veth
   │      │
 net-a  net-b
 NetNS  NetNS
```

A container that was not attached to `lab-net` at creation time can also be connected later:

```bash
docker run -dit --name net-c alpine sh
docker network connect lab-net net-c
docker network disconnect lab-net net-c
```

I deliberately used `net-c` here: `net-a` was already attached through `--network lab-net`, so it could not serve as an example of connecting to a network for the first time. A container can also be attached to multiple Docker networks at once.

From `net-a`, I verified connectivity to `net-b`:

```bash
docker exec -it net-a sh
ping -c 3 net-b
```

The name `net-b` can be used directly because user-defined bridge networks provide container name resolution. The data itself actually travels through the veth pairs and the Linux bridge.

## 5. Inspecting Network Configuration from Inside a Container

If Alpine lacks the necessary tools, install them first:

```bash
apk add --no-cache iproute2 iputils curl netcat-openbsd
```

Then inspect the interface, routes, neighbors, and DNS:

```bash
ip -4 addr show eth0
ip route
ip neigh
cat /etc/resolv.conf
```

The point is not to memorize the output but to understand how the pieces relate:

- `eth0` is a virtual network interface in the container's network namespace.
- Destinations in the same subnet can be reached directly; other destinations are usually handed to the default gateway.
- The neighbor table records the mapping between IP and MAC addresses on the same link.
- On a custom bridge network, `/etc/resolv.conf` often shows `127.0.0.11`, the address used by Docker's embedded DNS server.

## 6. Verifying Container-to-Container Communication with Sockets

In `net-b`, listen on a TCP port:

```bash
docker exec -it net-b sh
apk add --no-cache netcat-openbsd
nc -l 9000
```

In `net-a`, open a connection:

```bash
docker exec -it net-a sh
apk add --no-cache netcat-openbsd
nc net-b 9000
```

Once the connection is established, either side can send text directly. The communication path is:

```text
net-a: Socket
   ↓
eth0 → veth Pair → Linux Bridge → veth Pair → eth0
                                              ↓
                                        net-b:9000
```

This experiment needs no published host port, because both containers are already on the same virtual network.

I can also have each container listen on its own port `9000`. Ports belong to each network namespace separately, so using the same number does not cause a conflict.

## 7. NAT: Containers Reaching the Internet

In `net-a`, first check connectivity:

```bash
ping -c 3 google.com
ip -4 addr show eth0
ip route
```

On a regular bridge network, Docker has usually already configured the container's IP address, default gateway, forwarding, and outbound address translation, so a container with a private IP can still reach the internet.

I checked the source address seen from the internet, once from the container and once from macOS:

```bash
curl -4 --noproxy '*' https://api.ipify.org
echo
```

Here `-4` forces IPv4, and `--noproxy '*'` keeps curl from using any configured HTTP proxy.

The typical outbound path can be represented as:

```text
Container private IP
      ↓
veth Pair
      ↓
Linux Bridge
      ↓
Routing / SNAT
      ↓
External network
```

SNAT rewrites the source address, and the source port when necessary; conntrack records the connection and its translation state so that return packets can be delivered back to the original container.

That said, the current environment may involve several network layers at once: Docker's Linux networking, OrbStack, macOS, a home router, and so on. Looking up the public IP only verifies the address that is ultimately presented to the outside world; that result alone cannot tell which NAT rules each layer applied.

## 8. Port Mapping: Reaching a Container from macOS

I created an Nginx container and published the service only on the local loopback address:

```bash
docker run -d --rm \
  --name web-lab \
  --network lab-net \
  -p 127.0.0.1:8080:80 \
  nginx:alpine
```

`127.0.0.1:8080:80` means:

| Part | Meaning |
|---|---|
| `127.0.0.1` | Bind only to the host environment's loopback address |
| `8080` | The published port |
| `80` | The service port inside the container |

Access it from macOS and check the published mapping:

```bash
curl http://localhost:8080
docker port web-lab
```

The output looks similar to:

```text
80/tcp -> 127.0.0.1:8080
```

Meanwhile, `net-a`, which is on the same `lab-net`, can reach the container directly by name and internal port:

```bash
docker exec net-a wget -qO- http://web-lab:80
```

The two paths are not the same:

| Source | Request address | Depends on this `-p`? |
|---|---|---|
| macOS | `localhost:8080` | Yes |
| `net-a` | `web-lab:80` | No |

On a regular Linux Docker host, inbound port mapping is commonly implemented with mechanisms such as DNAT; in OrbStack, port forwarding between macOS and the Linux virtual environment also has to be handled.

Finally, view the access log and stop the lab container:

```bash
docker logs web-lab
docker stop web-lab
```

Because the container was created with `--rm`, it is removed automatically once it stops.

## 9. Working Backward from the Goal to the Command

After these experiments, I prefer to ask first "which kind of resource do I want to change?" and only then look for the specific option:

| Goal | Docker feature | Common commands |
|---|---|---|
| Access a host directory | Bind mount | `docker run --mount` |
| Persist application data | Volume | `docker volume`, `--mount` |
| Create a virtual LAN | Bridge network | `docker network create` |
| Attach a container to a network | Network attachment | `docker run --network`, `docker network connect` |
| Inspect network configuration | Inspect | `docker network inspect` |
| Let a container reach the internet | Routing + SNAT | Usually configured automatically by a regular bridge network |
| Let external clients reach a container service | Port mapping | `docker run -p` |
| Create a container and its main process | Run | `docker run` |
| Run a program in an existing container | Exec | `docker exec` |
| Check status and output | Observe | `docker ps`, `docker logs`, `docker inspect` |

When all the experiments are done, clean up the containers and network created along the way:

```bash
docker stop net-a net-b net-c
docker rm net-a net-b net-c
docker network rm lab-net
```

The named volume `mydata` is designed to outlive containers. Delete it with `docker volume rm mydata` only after confirming its data is no longer needed; removing volumes should not be a default step of container cleanup.

## 10. The Resulting Understanding

Docker does not reimplement filesystems or TCP/IP; it combines existing Linux mechanisms into container environments that are easy to manage:

```text
Linux Kernel
     │
     ├── Namespace: isolates views of resources such as PIDs, mounts, and networks
     ├── Cgroup: limits, accounts for, and schedules resource usage of process groups
     ├── Filesystem: OverlayFS, bind mounts, volumes, tmpfs
     └── Networking: veth, bridges, routing, NAT, port mapping
```

This hands-on work let me map abstract concepts to concrete behavior:

- Bind mounts show that a container can access shared files through a new path instead of having to copy data.
- The mount output for `/` and `/proc` shows that a container's directory tree is composed of different filesystems.
- Ping and Netcat show that isolated network namespaces can be reconnected through virtual devices.
- Internet access shows that private IP addresses can reach external networks through routing and address translation.
- Port mapping shows that a container can accept externally initiated connections without having a directly reachable address of its own.

The learning method I ended up with is: **first decide what environment I want the container to have, then find how Docker configures it, observe the result from inside and outside the container, and finally trace the behavior back to the underlying mechanism.**

There is no need to memorize every Docker command. Knowing whether I am configuring the filesystem, the network, or processes, and then looking up the specific options with `--help`, is usually more reliable than rote memorization.
