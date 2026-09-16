# Lab 04 — Root in a Container: What UID 0 Can and Cannot Do

## Goal

Produce evidence that:

1. a UID-0 process with the default container capability set cannot load
   modules, set the clock, or use `CAP_SYS_ADMIN` operations, but can chown and
   bind low ports;
2. dropping to the default set still leaves file ownership power over UID-0
   files;
3. a writable host bind mount grants host access to container root **without any
   capability**;
4. a user namespace removes both the capability power over host resources and
   the file-ownership power.

This lab **simulates** a container's privilege environment with `capsh`,
`unshare`, and `setpriv`, so it needs no container engine. If you have Docker,
optional commands show the same effects with real containers.

## Prerequisites

- Linux VM with `sudo`, `libcap2-bin`, `util-linux`, `python3`.
- Read: [4. Capabilities and user namespaces](../../../docs/05-capabilities/04-capabilities-and-user-namespaces.md)
  and [5. Root in a container](../../../docs/05-capabilities/05-root-in-a-container.md).

## Setup

```bash
DEFAULT="cap_chown,cap_dac_override,cap_fowner,cap_fsetid,cap_kill,cap_setgid,cap_setuid,cap_setpcap,cap_net_bind_service,cap_net_raw,cap_sys_chroot,cap_mknod,cap_audit_write,cap_setfcap"
# capsh --caps needs the +eip suffix on the whole set:
DEFAULT_EIP=$(echo "$DEFAULT" | sed 's/,/+eip,/g; s/$/+eip/')
runc_like() { sudo capsh --caps="$DEFAULT_EIP" -- -c "$1"; }
```

## Experiment

### Part A — The default set is UID 0 with 14 capabilities

```bash
runc_like 'id -u; capsh --print | grep -E "Current:|Bounding" '
```

### Part B — Allowed operations

```bash
runc_like 'touch /tmp/caplab-c && chown 1000 /tmp/caplab-c && echo "chown OK"; ls -ln /tmp/caplab-c'
runc_like 'python3 -c "import socket; s=socket.socket(); s.bind((\"0.0.0.0\", 80)); print(\"bind 80 OK\")"'
sudo rm -f /tmp/caplab-c
```

### Part C — Forbidden operations

**Predict first.** Which of these will fail?

```bash
runc_like 'date -s "2000-01-01" 2>&1 | tail -n1; echo "set clock exit: $?"'
runc_like 'modprobe dummy 2>&1 | tail -n1; echo "modprobe exit: $?"'
runc_like 'mount -t tmpfs none /mnt 2>&1 | tail -n1; echo "mount exit: $?"'
runc_like 'sysctl -w kernel.hostname=pwned 2>&1 | tail -n1; echo "sysctl exit: $?"'
```

### Part D — Ownership beats a reduced capability set

Create a host secret owned by root, then enter the default set and try to read
it two ways:

```bash
echo "host root secret" | sudo tee /root/caplab-secret >/dev/null
sudo chmod 600 /root/caplab-secret
runc_like 'cat /root/caplab-secret 2>&1'
```

**Predict first.** The default set includes `CAP_DAC_OVERRIDE`. Will `cat`
succeed? Would it succeed if `CAP_DAC_OVERRIDE` were dropped?

```bash
sudo capsh --caps="cap_chown+eip" -- -c 'cat /root/caplab-secret 2>&1'          # only chown, run as root
sudo rm -f /root/caplab-secret
```

### Part E — The user namespace changes everything

Create a file owned by real root, then try to modify it (a) as simulated
container root with default caps, (b) inside a user namespace mapping container
root to your unprivileged UID:

```bash
echo "v1" | sudo tee /tmp/caplab-hostfile >/dev/null
sudo chown 0:0 /tmp/caplab-hostfile; sudo chmod 644 /tmp/caplab-hostfile

runc_like 'echo v2-from-default-root > /tmp/caplab-hostfile && echo "default root wrote it"'
cat /tmp/caplab-hostfile

unshare --user --map-root-user bash -c 'id; echo v3-from-userns-root > /tmp/caplab-hostfile 2>&1; echo "userns root write exit: $?"'
cat /tmp/caplab-hostfile
sudo rm -f /tmp/caplab-hostfile
```

**Predict first.** In which case can "container root" overwrite the
root-owned file?

### Part F (optional, needs Docker) — The real thing

```bash
docker run --rm alpine sh -c 'apk add -q libcap 2>/dev/null; capsh --print 2>/dev/null | grep Current || grep Cap /proc/self/status'
docker run --rm alpine sh -c 'date -s "2000-01-01"; echo "exit: $?"'
docker run --rm --privileged alpine sh -c 'date -s "2000-01-01" >/dev/null 2>&1 && date && echo "privileged could set the clock (reverting)"; hwclock --hctosys 2>/dev/null'
docker run --rm -v /etc:/hostetc:ro alpine cat /hostetc/hostname
docker run --rm --user 1000 --cap-add NET_BIND_SERVICE alpine sh -c 'nc -lp 80 2>&1 | head -n1 || echo "cap-add had no effect for non-root user"'
```

## Expected observations

**Part A.** `id -u` is `0`; `capsh --print` shows exactly the 14 capabilities in
`Current:` and `Bounding`.

**Part B.** Both succeed: `chown OK` with the file owned by `1000`, and
`bind 80 OK`.

**Part C.** All four fail. `date`: `cannot set date: Operation not permitted`.
`modprobe`: `Operation not permitted`. `mount`: `permission denied` /
`Operation not permitted`. `sysctl`: `permission denied` (needs `CAP_SYS_ADMIN`).

**Part D.** With the default set, `cat` **succeeds** (`CAP_DAC_OVERRIDE`). With
only `cap_chown`, `cat` fails with `Permission denied`: it is UID 0 but the file
is owned by root with mode 600 and no capability overrides it — wait: UID 0 is
the owner, so it can read via owner bits. **Observe carefully:** with `600` and
owner root, even `cap_chown`-only root reads it through **ownership**. To see a
denial, the file must be owned by a *different* UID; repeat with
`sudo chown 1000 /root/caplab-secret` before the `cap_chown+eip` run.

**Part E.** Default "container root" (real UID 0) overwrites the file:
`default root wrote it`, contents `v2-from-default-root`. Inside the user
namespace, `id` shows `uid=0(root)` but the write **fails** with
`Permission denied`, because the process is really your unprivileged UID and the
file is owned by real root, which is unmapped.

**Part F.** The Alpine container shows the 14-capability set; setting the clock
fails; the privileged container can set it; the read-only `/etc` bind mount
reveals the host's hostname; `--cap-add NET_BIND_SERVICE` with `--user 1000`
does not let the non-root user bind port 80 (section 3).

## Why this happens

- **B, C.** Each operation calls `ns_capable()` for a specific capability; only
  the 14 present ones pass, and only relative to namespaces the process owns.
- **D.** File reads first check ownership bits; `CAP_DAC_OVERRIDE` only matters
  when ownership does not already grant access.
- **E.** Real UID 0 satisfies ownership and `CAP_DAC_OVERRIDE` against a
  root-owned file. In the user namespace, the kernel's real UID is unprivileged
  and root's files are unmapped, so neither ownership nor `CAP_DAC_OVERRIDE`
  applies.

## Connection to containers

- Parts B–D are the day-to-day meaning of the default capability set.
- Part D and E together are the crucial security lesson: **without a user
  namespace, container root's power over mounted host files comes from
  ownership, which capabilities cannot remove.** A read-only mount or a
  non-root user or a user namespace is required.
- Part F confirms the simulation matches a real engine.

## Questions to think about

1. A container must run `chrony`/`ntpd` to set the clock. Which capability does
   it need, and why is granting it risky on a shared host?
2. You must mount a host log directory into a container that runs as root.
   List three independent ways to prevent the container from tampering with host
   files there.
3. Why does enabling a user namespace let you safely give a container **more**
   capabilities than you would without one?
4. Explain, using this chapter, why `--privileged` is so much more dangerous
   than `--cap-add ALL`.
