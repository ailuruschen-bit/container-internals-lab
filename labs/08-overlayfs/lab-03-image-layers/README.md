# Lab 03 — Real Image Layers Are OverlayFS Layers

## Goal

Produce evidence that:

1. a container image is a set of layer tarballs, each a filesystem diff;
2. a running container's rootfs is an overlay mount over the unpacked layers;
3. the container's changes live in the overlay `upperdir` (its diff);
4. image-format whiteouts (`.wh.`) differ from OverlayFS whiteouts (0/0 char
   devices).

This lab needs **Docker** (root). With Podman, adjust paths (its overlay lives
under `~/.local/share/containers` or `/var/lib/containers`). If you have neither,
read the expected observations; Labs 01–02 already proved the mechanism.

## Prerequisites

- Linux VM with Docker and `sudo`, `jq`, `tar`, `util-linux`.
- Read: [3. Image layers and snapshotters](../../../docs/08-overlayfs/03-image-layers.md).

## Experiment

### Part A — An image is layers

```bash
docker pull alpine >/dev/null
docker history alpine
docker save alpine -o /tmp/alpine.tar
mkdir -p /tmp/alpine-img && tar -xf /tmp/alpine.tar -C /tmp/alpine-img
ls /tmp/alpine-img
cat /tmp/alpine-img/manifest.json | jq '.[0].Layers' 2>/dev/null || cat /tmp/alpine-img/manifest.json
```

### Part B — A running container is an overlay mount

```bash
CID=$(docker run -d alpine sleep 600)
PID=$(docker inspect -f '{{.State.Pid}}' "$CID")
sudo grep ' / ' /proc/$PID/mountinfo | grep overlay
docker inspect -f '{{json .GraphDriver.Data}}' "$CID" | jq .
```

**Predict first.** Will the mount's filesystem type be `overlay`? How many
`lowerdir` entries for a single-layer image like Alpine?

### Part C — Container changes land in upperdir

```bash
docker exec "$CID" sh -c 'echo "hello from container" > /root/note.txt; rm /etc/motd 2>/dev/null; echo done'
UPPER=$(docker inspect -f '{{.GraphDriver.Data.UpperDir}}' "$CID")
echo "upperdir: $UPPER"
sudo ls -lR "$UPPER" | head -n 30
sudo cat "$UPPER/root/note.txt"
docker diff "$CID"
```

### Part D — Whiteout representations

```bash
# In the overlay upper (on disk): a deleted file is a 0/0 char device.
docker exec "$CID" rm -f /etc/hostname 2>/dev/null || true
sudo find "$UPPER" -type c -exec ls -l {} \; 2>/dev/null | head

# In the image tar (the format): deletions are .wh. files. Build a layer that deletes something.
printf 'FROM alpine\nRUN rm /etc/motd || true\n' > /tmp/Dockerfile.wh
docker build -q -t wh-demo -f /tmp/Dockerfile.wh /tmp >/dev/null
docker save wh-demo -o /tmp/wh.tar
mkdir -p /tmp/wh && tar -xf /tmp/wh.tar -C /tmp/wh
# Find the top layer tar and look for .wh. entries
for l in $(find /tmp/wh -name '*.tar' -o -name 'layer.tar'); do echo "== $l"; tar -tf "$l" | grep -i '\.wh\.' && break; done
```

### Cleanup

```bash
docker rm -f "$CID" >/dev/null; docker rmi wh-demo >/dev/null 2>&1
rm -rf /tmp/alpine.tar /tmp/alpine-img /tmp/wh.tar /tmp/wh /tmp/Dockerfile.wh
```

## Expected observations

**Part A.** `docker history` lists the layers/instructions. The extracted image
contains a `manifest.json`, per-layer directories or blobs, and a config JSON.
`manifest.json` lists one or more `Layers` (Alpine: one).

**Part B.** The rootfs mount is type `overlay`. `GraphDriver.Data` shows
`LowerDir`, `UpperDir`, `MergedDir`, `WorkDir` paths under
`/var/lib/docker/overlay2/...`. Alpine (one layer) has a short `LowerDir`.

**Part C.** `UpperDir` contains `root/note.txt` with the text, and the diff of
whatever else changed. `docker diff` lists `A /root/note.txt` and, if you removed
files, `D` entries. The `upperdir` is exactly the container's changes.

**Part D.** In `upperdir`, deleted files appear as character devices `0, 0`
(`find -type c`). In the built image's top layer **tar**, the deletion appears
as a regular entry named `.wh.motd`. Two different representations of the same
idea.

## Why this happens

- Docker's `overlay2` driver unpacks each image layer into
  `overlay2/<id>/diff`, then mounts an overlay with those as `LowerDir` and a
  per-container `UpperDir`/`WorkDir`, exposing `MergedDir` as the rootfs.
- Container writes and deletes land in `UpperDir` as copy-ups, new files, and
  0/0 whiteouts (Lab 02).
- The image tar format records deletions as `.wh.` files; the driver translates
  between the two when saving/loading.

## Connection to containers

- This closes the loop from Labs 01–02: the overlay you built by hand is exactly
  what Docker builds for every container.
- `docker diff` = the `upperdir` contents; `docker commit` = turn `upperdir`
  into a new image layer.
- Chapter 12 (containerd) generalizes `overlay2` into the snapshotter interface.

## Questions to think about

1. Two containers run from `alpine`. How much of the ~5 MB image is duplicated on
   disk between them? Which directory differs?
2. You `rm` a 100 MB file from the base image inside a running container. What
   does `docker diff` show, and does the host reclaim 100 MB?
3. Why must the driver translate `.wh.` image entries into 0/0 char devices when
   unpacking, instead of leaving the `.wh.` files in place?
