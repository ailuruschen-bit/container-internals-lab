# 5. Root in a Container

## The question

> Is "root inside a container" the same as "root on the host"?

After sections 1–4 the answer can be given precisely: **it depends on the
capability sets, on whether a user namespace is used, and on what the container
can reach**. In the default configuration it is neither harmless nor equivalent
to host root.

## The default capability set

Docker, containerd (and therefore most Kubernetes nodes), and Podman start
container processes with a short allow-list. Docker's and containerd's default:

| Capability | Why it is in the default set |
|---|---|
| `CHOWN` | package managers and entrypoints change file ownership |
| `DAC_OVERRIDE` | root-style file access inside the image |
| `FOWNER`, `FSETID` | mod