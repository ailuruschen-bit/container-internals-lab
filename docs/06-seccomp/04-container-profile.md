# 4. The Container seccomp Profile

## What the default profile is

Docker, containerd, and Kubernetes ship a **default seccomp profile**: a JSON
document, applied to every container unless overridden. Its structure mirrors the
OCI `linux.seccomp` field:

```json
{
  "defaultAction": "SCMP_ACT_ERRNO",
  "defaultErrnoRet": 1,
  "architectures": ["SCMP_ARCH_X86_64", "SCMP_ARCH_X86", "SCMP_ARCH_X32"],
  "syscalls": [
    { "names": ["read", "write", "openat", "close", "..."],
      "action": "SCMP_ACT_ALLOW" },
    { "names": ["clone"],
      "action": "SCMP_ACT_ALLOW",
      "args": [ { "index": 0, "value": 2114060288, "op": "SCMP_CMP_MASKED_EQ" } ] }
  ]
}
```

Reading it with the chapter's vocabulary:

- **Allow list.** The default action is `ERRNO` (return an error, not kill), and
  the `syscalls` array lists the roughly **300+** syscalls that are allowed. Any
  syscall not listed returns `EPERM`.
- **Architectures.** Listed explicitly, so the architecture check (section 2) is
  handled, and non-listed architectures are denied.
- **Argument rules.** A handful of entries use `args` to restrict specific calls,
  historically `clone`/`unshare` new-namespace flags, `personality`, and some
  `ptrace`/`bpf`/keyring calls, allowed only when a matching capability is
  present.

## What it blocks, and why

The default profile blocks roughly **50–60** syscalls out of the ~400 the kernel
provides. The blocked set is chosen to remove **attack surface** and rarely used
privileged operations, for example:

| Blocked (examples) | Why |
|---|---|
| `mount`, `umount2`, `pivot_root`, `swapon` | filesystem control; need `CAP_SYS_ADMIN` and can affect the host |
| `reboot`, `kexec_load` | can take down the host |
| `init_module`, `finit_module`, `delete_module` | load kernel code |
| `settimeofday`, `clock_settime`, `adjtimex` | change the host clock |
| `ptrace` (restricted historically), `process_vm_readv/writev` | inspect other processes |
| `keyctl`, `add_key`, `request_key` | kernel keyring (namespacing issues) |
| `perf_event_open`, `bpf` (restricted) | large kernel attack surface |
| `userfaultfd`, `unshare`/`clone` with `CLONE_NEWUSER` (historically) | escalation techniques |

Crucially, the profile is **defense in depth alongside capabilities**, not a
replacement. Many blocked syscalls would also fail from a missing capability;
seccomp blocks them **before** the syscall runs, shrinking the code an attacker
can reach even through a capability they do hold.

## Interaction with capabilities

The two mechanisms are orthogonal and combine:

```text
syscall attempt
   │
   ├─ seccomp filter runs first ──► ERRNO/KILL if the syscall is not allowed
   │
   └─ if allowed, the syscall runs ──► capability checks inside it ──► EPERM if the capability is missing
```

So a container can be blocked from an operation in **two** independent ways, and
the default profile deliberately relaxes some rules **based on capabilities**:
for example, some profiles allow `ptrace` only if the container was granted
`CAP_SYS_PTRACE`. Removing either layer still leaves the other.

## Overriding the profile

- **`--security-opt seccomp=unconfined`** (Docker) or an `Unconfined` seccomp
  profile (Kubernetes) disables it entirely. This is a large increase in attack
  surface and should be rare.
- **A custom profile** (`--security-opt seccomp=/path.json`, or Kubernetes
  `localhostProfile`) can allow extra syscalls a specific workload needs, or
  tighten the default. The Kubernetes `RuntimeDefault` seccomp profile applies
  the container runtime's default; the Restricted Pod Security Standard requires
  either `RuntimeDefault` or `Localhost`.
- Since Kubernetes 1.27, `seccompDefault` can make `RuntimeDefault` the
  cluster-wide default instead of `Unconfined`.

## A note on LSMs: the orthogonal fifth layer

Chapters 03–06 cover the kernel primitives runtimes use directly. **Linux
Security Modules (LSMs)**, chiefly **AppArmor** and **SELinux**, add another,
independent layer that this repository does not cover in depth but you should
know exists:

- They enforce **mandatory access control**: rules, set by an administrator, that
  even root cannot override, keyed on program **paths**, file labels, network
  operations, and capabilities.
- They *can* make path-based decisions that seccomp cannot (section 3): "this
  container profile may read `/etc` but not `/host`".
- Docker ships a default **AppArmor** profile (`docker-default`); Red Hat
  platforms use **SELinux** with per-container **MCS labels** so that even a
  container escape as root is confined by the label.
- The relevant capabilities are `CAP_MAC_ADMIN` and `CAP_MAC_OVERRIDE`
  (Chapter 05 §1).

The full confinement of a modern container is therefore: namespaces + cgroups +
capabilities + seccomp + an LSM, each independent, each able to stop what the
others miss, all sharing one kernel.

## Why this matters for containers

- The seccomp profile is one line of configuration but hundreds of syscall
  decisions. When a containerized program mysteriously fails with `EPERM` on a
  syscall it should be allowed to make, the profile is a prime suspect; check
  with `strace` (Chapter 01) and compare against the profile.
- Understanding that seccomp is an **allow list with an `ERRNO` default** explains
  why unusual, new, or architecture-specific syscalls sometimes break in
  containers after a kernel or libc upgrade.

## Evidence

Lab: [`lab-04-default-profile`](../../labs/06-seccomp/lab-04-default-profile/)

## Further Reading

- Docker: [Seccomp security profiles for Docker](https://docs.docker.com/engine/security/seccomp/)
  — how the default profile works and how to supply a custom one.
- moby default profile source:
  [`profiles/seccomp/default.json`](https://github.com/moby/moby/blob/master/profiles/seccomp/default.json)
  and the generator [`profiles/seccomp/seccomp_default.go`](https://github.com/moby/moby/blob/master/profiles/seccomp/seccomp_default.go)
  — the authoritative allow list and its capability-conditioned rules.
- Kubernetes: [Restrict a Container's Syscalls with seccomp](https://kubernetes.io/docs/tutorials/security/seccomp/)
  and [Seccomp and Kubernetes](https://kubernetes.io/docs/reference/node/seccomp/).
- OCI Runtime Specification: [config-linux.md — Seccomp](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#seccomp)
  — the `linux.seccomp` schema the profile compiles to (Chapter 10).
- AppArmor: [Docker AppArmor security profiles](https://docs.docker.com/engine/security/apparmor/).
  SELinux: [Red Hat, "Introduction to SELinux for containers"](https://www.redhat.com/en/blog/introduction-using-selinux-container-runtimes).
