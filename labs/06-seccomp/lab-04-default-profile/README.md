# Lab 04 — The Default Container seccomp Profile

## Goal

Produce evidence that:

1. a default container has seccomp filter mode active;
2. the default profile blocks certain syscalls even for container root;
3. removing the profile (`seccomp=unconfined`) lets those syscalls through
   (but capabilities and namespaces may still block the operation);
4. seccomp and capabilities are independent layers.

This lab needs **Docker** (or `podman`, adjusting commands). If you have no
container engine, the mechanism is already shown by Labs 02–03; read the
expected observations here as a description.

## Prerequisites

- Linux VM with Docker (or Podman), `sudo`.
- Read: [4. The container seccomp profile](../../../docs/06-seccomp/04-container-profile.md).

## Experiment

### Part A — Seccomp is on by default

```bash
docker run --rm alpine grep Seccomp /proc/1/status
```

**Predict first.** What value of `Seccomp:` do you expect?

### Part B — A blocked syscall, with and without the profile

`unshare(CLONE_NEWNS)` (a new mount namespace) needs `CAP_SYS_ADMIN`, which a
default container lacks, **and** `unshare` with namespace flags is restricted by
the profile. Compare the two failures:

```bash
docker run --rm alpine unshare -m echo hi; echo "default exit: $?"
docker run --rm --security-opt seccomp=unconfined alpine unshare -m echo hi; echo "unconfined exit: $?"
docker run --rm --security-opt seccomp=unconfined --cap-add SYS_ADMIN alpine unshare -m echo "mount ns ok"; echo "unconfined+cap exit: $?"
```

**Predict first.** Which of the three succeeds?

### Part C — A clearly seccomp-only block

`chroot()` needs `CAP_SYS_CHROOT`, which the default set **has**, but let us pick
a syscall the profile blocks while its capability may be present. `swapon`/`
swapoff` are blocked by the profile and also need `CAP_SYS_ADMIN`; `perf_event_
open` is blocked by the profile. Use a syscall probe:

```bash
docker run --rm alpine sh -c 'apk add -q strace 2>/dev/null; strace -f -e trace=perf_event_open perf_event_open 2>&1 | head' 2>/dev/null || \
docker run --rm ubuntu sh -c 'apt-get -qq update >/dev/null 2>&1; apt-get -qq install -y strace >/dev/null 2>&1; python3 - <<PY
import ctypes,ctypes.util,os
libc=ctypes.CDLL(None,use_errno=True)
# perf_event_open is syscall 298 on x86_64; call it raw to see the errno
r=libc.syscall(298,0,0,0,0,0)
print("perf_event_open returned",r,"errno",os.strerror(ctypes.get_errno()))
PY'
docker run --rm --security-opt seccomp=unconfined ubuntu python3 -c '
import ctypes,os
libc=ctypes.CDLL(None,use_errno=True)
r=libc.syscall(298,0,0,0,0,0)
print("unconfined perf_event_open returned",r,"errno",os.strerror(ctypes.get_errno()))'
```

### Part D — Capabilities and seccomp are independent

```bash
# Drop all caps but keep default seccomp:
docker run --rm --cap-drop ALL alpine id
# All caps (privileged-ish) but keep default seccomp still blocks profile syscalls:
docker run --rm --cap-add ALL ubuntu python3 -c '
import ctypes,os
libc=ctypes.CDLL(None,use_errno=True)
print("with all caps, default seccomp: perf_event_open errno",
      (libc.syscall(298,0,0,0,0,0), os.strerror(ctypes.get_errno()))[1])'
```

## Expected observations

**Part A.** `Seccomp: 2` (filter mode). (`Seccomp_filters: 1` on kernels that
report it.)

**Part B.** `unshare -m` fails in the first two runs. With the default profile
the error is typically `Operation not permitted` from the blocked syscall; with
`seccomp=unconfined` but no `CAP_SYS_ADMIN`, it still fails, now purely from the
missing capability. Only the third run (`unconfined` **and** `--cap-add
SYS_ADMIN`) prints `mount ns ok`.

**Part C.** With the default profile, the raw `perf_event_open` returns `-1` with
errno `Operation not permitted` (`EPERM`), because the profile's default action
is `ERRNO(EPERM)`. With `seccomp=unconfined`, the same call returns `-1` with a
*different* errno such as `Invalid argument` (`EINVAL`) — proof the syscall now
reaches the kernel and fails for its own reasons, not because seccomp blocked it.

**Part D.** `--cap-drop ALL` still runs `id` (no capability needed for it). With
`--cap-add ALL` but the default profile, `perf_event_open` still returns `EPERM`:
having the capability does not help, because seccomp blocks the syscall before
the capability check.

## Why this happens

- The runtime installs the default profile (default action `ERRNO(EPERM)`) plus
  the dropped capability set before `execve()`.
- A syscall not in the allow list returns `EPERM` from seccomp, regardless of
  capabilities (Part D). A syscall that **is** allowed then undergoes normal
  capability checks (Part B, unconfined case).
- The change in errno between confined and unconfined (Part C) is the clean
  signal that seccomp, not the syscall's own logic, caused the confined failure.

## Connection to containers

- This is the fifth confinement layer in action, independent of the capability
  layer from Chapter 05.
- Part C's "errno changes when unconfined" is the practical way to tell whether a
  container failure is caused by seccomp or by something else.
- Part B shows why fixing a seccomp block often also requires a capability, and
  why `--privileged` (which relaxes both) makes problems disappear but is unsafe.

## Questions to think about

1. A container fails a syscall with `EPERM`. How would you determine whether the
   cause is seccomp, a missing capability, or a namespace restriction?
2. Why is disabling seccomp (`seccomp=unconfined`) more dangerous than adding one
   capability, even though both "relax" the container?
3. When is a **custom** seccomp profile (allowing a few extra syscalls) a better
   choice than `unconfined`?
