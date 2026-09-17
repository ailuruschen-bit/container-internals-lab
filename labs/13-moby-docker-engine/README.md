# Labs — Chapter 13: Moby / Docker Engine

| Lab | Topic | Needs | Doc section |
|---|---|---|---|
| [lab-01-docker-layers](lab-01-docker-layers/) | trace `docker run` through dockerd→containerd→shim→runc→kernel; confirm the isolation | Docker, root | [§1](../../docs/13-moby-docker-engine/01-architecture.md), [§4](../../docs/13-moby-docker-engine/04-docker-run-nginx.md) |
| [lab-02-docker-networking](lab-02-docker-networking/) | docker0, veth, masquerade, DNAT = Chapter 03 Lab 06 automated | Docker, root | [§2](../../docs/13-moby-docker-engine/02-networking.md) |
| [lab-03-build-an-image](lab-03-build-an-image/) | instructions→layers; content-addressed cache; it is an OCI image | Docker | [§3](../../docs/13-moby-docker-engine/03-build.md) |

Setup:

```bash
# Install Docker Engine per https://docs.docker.com/engine/install/
sudo apt-get install -y jq iproute2 iptables procps
```

Lab 01 is the capstone: it demonstrates the entire stack on your machine. Written
for Docker Engine 27.x; container PIDs, cgroup paths, and iptables/nft output vary
by version and host configuration.
