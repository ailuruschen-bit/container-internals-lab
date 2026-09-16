# Lab 04 — Mount Propagation: Shared, Slave, Private, Unbindable

## Goal

Produce evidence that:

1. bind-mounting a shared mount creates a peer that exchanges mount events in
   both directions;
2. a slave receives events from its master but sends none back;
3. a private mount neither sends nor receives events;
4. an unbindable mount cannot be bind-mounted;
5. unmount events propagate the same way as mount events;
6. your system's default propagation is visible in `mountinfo`.

## Prerequisites

- Linux VM with `sudo` and `util-linux`.
- Read: [4. Mount propagation](../../../docs/02-linux-filesystem/04-mount-propagation.md).

## Background

We create one tmpfs at `A`, make it shared, and bind-mount it three times:

```text
A  (shared)  ─┬─ B  peer of A                (stays shared)
              ├─ C  made slave  → master is A's peer group
              └─ D  made private
```

Then we mount small tmpfs instances ("events") inside each of them and observe
where they appear.

## Experiment

### Part A — What does your system use by default?

```bash
findmnt -o TARGET,PROPAGATION / /tmp /proc /sys
grep -E ' / / ' /proc/self/mountinfo
```

### Part B — Build the four mounts

```bash
mkdir -p /tmp/proplab/{A,B,C,D,E} && cd /tmp/proplab
sudo mount -t tmpfs prop-base A
sudo mount --make-shared A
sudo mkdir A/x A/y A/z A/w

sudo mount --bind A B
sudo mount --bind A C && sudo mount --make-slave C
sudo mount --bind A D && sudo mount --make-private D

findmnt -R -o TARGET,SOURCE,PROPAGATION /tmp/proplab
grep proplab /proc/self/mountinfo | awk '{ for (i=7; $i != "-"; i++) tags = tags " " $i; print $5, "→", tags; tags="" }'
```

Look at the optional fields: `A` and `B` should carry the same `shared:N`; `C`
should carry `master:N` with the same `N`; `D` should carry nothing.

### Part C — Events from the shared mount A

**Predict first.** A tmpfs is mounted at `A/x`. At which of `B/x`, `C/x`,
`D/x` will it appear?

```bash
sudo mount -t tmpfs event-x A/x
findmnt -R -o TARGET,SOURCE /tmp/proplab | grep event-x
```

### Part D — Events from the peer B

```bash
sudo mount -t tmpfs event-y B/y
findmnt -R -o TARGET,SOURCE /tmp/proplab | grep event-y
```

### Part E — Events from the slave C

**Predict first.** Will `event-z` appear at `A/z`?

```bash
sudo mount -t tmpfs event-z C/z
findmnt -R -o TARGET,SOURCE /tmp/proplab | grep event-z
```

### Part F — Events from the private D

```bash
sudo mount -t tmpfs event-w D/w
findmnt -R -o TARGET,SOURCE /tmp/proplab | grep event-w
```

### Part G — Unmount events propagate too

```bash
sudo umount /tmp/proplab/A/x
findmnt -R -o TARGET,SOURCE /tmp/proplab | grep event-x || echo "event-x is gone everywhere"
```

### Part H — Unbindable

```bash
sudo mount --make-unbindable D
sudo mount --bind D E; echo "exit code: $?"
findmnt -o TARGET,PROPAGATION /tmp/proplab/D
```

### Cleanup

```bash
cd /tmp
sudo umount -R /tmp/proplab/D /tmp/proplab/C /tmp/proplab/B /tmp/proplab/A
findmnt -R /tmp/proplab || echo "no mounts left"
rm -rf /tmp/proplab
```

## Expected observations

**Part A.** On systemd-based distributions: `shared` for `/`, `/proc`, `/sys`,
and usually `/tmp`. The root line contains `shared:1` (the exact number may
differ).

**Part B.**

```text
/tmp/proplab/A → shared:301
/tmp/proplab/B → shared:301
/tmp/proplab/C → master:301
/tmp/proplab/D →
```

If `/tmp` is itself shared, you may see additional mounts propagated by it;
focus on the tags.

**Part C.** `event-x` appears at `A/x`, `B/x`, and `C/x`, but **not** at `D/x`.

**Part D.** `event-y` appears at `B/y`, `A/y`, and `C/y`, but not at `D/y`.
The peer sends events back to `A`, which is what makes it a peer.

**Part E.** `event-z` appears **only** at `C/z`.

**Part F.** `event-w` appears **only** at `D/w`.

**Part G.** `event-x` disappears from `A/x`, `B/x`, and `C/x` together.

**Part H.** The bind mount fails with an error such as
`mount: /tmp/proplab/E: wrong fs type, bad option, bad superblock...` and a
non-zero exit code; the kernel returned `EINVAL`. `findmnt` shows
`unbindable`.

## Why this happens

- `mount --bind` of a shared mount adds the new mount to the source's peer
  group (`shared:301`).
- `--make-slave` removes `C` from the peer group and records the group as its
  master. Propagation from a master to a slave is one-directional.
- `--make-private` removes `D` from all propagation relationships.
- The kernel's `propagate_mnt()` (mount events) and `propagate_umount()`
  (unmount events) walk peer groups and slaves of the parent mount where the
  event happened.
- `MS_UNBINDABLE` makes `do_loopback()` refuse to use the mount as a bind
  source.

## Connection to containers

- Replace `A` with "the host's `/`" and `C` with "the container's copy of it
  after runc runs `--make-rslave`". Part C is a host mount becoming visible in
  the container. Part E is a container mount that must not leak to the host.
- Part D is `rshared` / Kubernetes `Bidirectional` propagation: mounts made in
  the "container" appear on the "host".
- Part F is `rprivate`, Docker's default for bind volumes.
- Unbindable mounts are used to prevent mount trees from multiplying when a
  directory is recursively bind-mounted into a subdirectory of itself.

## Questions to think about

1. A container storage plugin mounts a volume inside its container, but the
   application container on the same host cannot see it. Which propagation
   types would you check, on which mounts?
2. After Part E, run `sudo mount --make-shared C`. `C` is now "shared and
   slave". Where would a new mount below `C` propagate?
3. Why do you think systemd chose `shared` as the system default, even though
   the kernel default is `private`?
4. Why must `Bidirectional` propagation be limited to privileged containers?
