# 6. Capabilities and seccomp

## What we add

In `child.go`, three calls just before `execve()`:

```go
dropCapabilities()   // caps.go   (Ch. 05)
setNoNewPrivs()      // caps.go   (Ch. 06 §1)
installSeccomp()     // seccomp.go (Ch. 06)
```

Order matters and follows Chapter 05 §3 and Chapter 06 §1: reduce privilege
**last**, right before exec, so the setup steps (which needed privilege) have
already run; and set `no_new_privs` before seccomp so the filter installs without
`CAP_SYS_ADMIN`.

## Capabilities

`caps.go` drops a curated list from the **bounding set** with
`prctl(PR_CAPBSET_DROP, cap)`:

```go
drop := []uintptr{capSysAdmin, capSysModule, capSysTime, capSysBoot,
    capSysRawio, capSysPtrace, capNetAdmin, capMknod, capSysResource, capSyslog}
```

Dropping from the **bounding set** is the guarantee from Chapter 05 §3: even a
setuid-root binary the container execs cannot regain these. A full runtime also
rewrites effective/permitted/inheritable via `capset()` and applies the OCI
lists exactly; `minic` keeps to the bounding set to stay dependency-free and
readable, which is enough to demonstrate the effect.

## no_new_privs

`setNoNewPrivs` is `prctl(PR_SET_NO_NEW_PRIVS, 1)` (Chapter 06 §1): a one-way
flag that stops `execve()` from granting new privileges and lets us install
seccomp unprivileged.

## seccomp

`seccomp.go` builds a **classic BPF** filter by hand (Chapter 06 §2), using only
the standard library. It is a **deny list**: default `ALLOW`, a few syscalls
(`mount`, `umount2`, `init_module`, `ptrace`, `keyctl`) return `EPERM`, and a
wrong architecture returns `KILL_PROCESS`. The filter checks `arch` first
(Chapter 06 §2's critical pitfall) and uses per-architecture syscall numbers.

A real profile is an **allow list** of ~300 syscalls compiled by libseccomp;
`minic`'s deny list is the inverse, chosen so the shell keeps working while a few
operations are visibly blocked.

## What changed

Run `sudo ./minic run -- /bin/sh` and inside:

- `grep -E 'CapBnd|NoNewPrivs|Seccomp' /proc/self/status`: `CapBnd` is missing
  the dropped bits, `NoNewPrivs: 1`, `Seccomp: 2` (filter mode).
- `mount -t tmpfs none /mnt` fails with `Operation not permitted` (blocked by
  seccomp **and** by the dropped `CAP_SYS_ADMIN` — two independent layers,
  Chapter 06 §4).
- A setuid-root binary in the rootfs no longer escalates (`no_new_privs`,
  Chapter 05 Lab 02 / Chapter 06 Lab 01).
- `date -s` (needs `CAP_SYS_TIME`, dropped) fails.

## What has NOT changed

- The container is **still UID 0** unless you used `-userns`. Its confinement is
  now namespaces + cgroups + reduced capabilities + seccomp, but a **writable
  bind mount of a host path would still grant host access through ownership**
  (Chapter 05 §5). `minic` does not add bind-mounted volumes, so this risk is
  latent, not present, but the lesson stands.
- The kernel is shared; a kernel bug reachable through an allowed syscall is
  still reachable (Chapter 06 §5). `-userns` (§7) is what makes an escape land as
  an unprivileged host user.

## Verifying with strace

From the host, `sudo strace -f -e trace=prctl,seccomp,capset -p <host-pid>` (or
strace the whole run) shows the `PR_CAPBSET_DROP` calls, `PR_SET_NO_NEW_PRIVS`,
and the `seccomp`/`prctl(PR_SET_SECCOMP)` call, in the order above — the same
order runc uses (Chapter 05 §3).

## Further Reading

- Chapter 05 [§3 transformation](../05-capabilities/03-capabilities-across-execve.md)
  and [§5 root in a container](../05-capabilities/05-root-in-a-container.md).
- Chapter 06 [§1 no_new_privs](../06-seccomp/01-no-new-privs.md),
  [§2 filters](../06-seccomp/02-seccomp-modes-and-filters.md),
  [§4 the default profile](../06-seccomp/04-container-profile.md).
- Kernel docs: [Seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html)
  — to check `seccomp.go`'s hand-built filter against the specification.
