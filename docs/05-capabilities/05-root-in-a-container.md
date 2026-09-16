# 5. Root in a Container

This section assembles the chapter into the answer to a question every container
user eventually asks: **why is root inside a container not the same as root on
the host?** And the equally important follow-up: **when is it?**

## The default capability set

A default Docker or containerd container (no `--privileged`, no user namespace)
runs its process as UID 0 with a **bounding and effective set of 14
capabilities**:

```text
CAP_CHOWN            CAP_DAC_OVERRIDE     CAP_FOWNER          CAP_FSETID
CAP_KILL             CAP_SETGID           CAP_SETUID          CAP_SETPCAP
CAP_NET_BIND_SERVICE CAP_NET_RAW          CAP_SYS_CHROOT      CAP_MKNOD
CAP_AUDIT_WRITE      CAP_SETFCAP
```

Compare with what is **dropped** from full root:

```text
CAP_SYS_ADMIN     CAP_SYS_MODULE    CAP_SYS_TIME      CAP_SYS_BOOT
CAP_SYS_RAWIO     CAP_SYS_PTRACE    CAP_NET_ADMIN     CAP_SYS_NICE
CAP_SYS_RESOURCE  CAP_SYSLOG        CAP_MAC_ADMIN     CAP_MAC_OVERRIDE
CAP_AUDIT_CONTROL CAP_DAC_READ_SEARCH  CAP_LINUX_IMMUTABLE ... (about 26 more)
```

(Kubernetes drops even more by default in some configurations, and Docker's
list has changed slightly across versions, for example `CAP_NET_RAW` is now
sometimes dropped. Always check `capsh --decode` on the actual container.)

## What the default set allows and forbids

| A container's root process can... | Because it has... |
|---|---|
| change ownership of files (in its mounts) | `CAP_CHOWN`, `CAP_FOWNER` |
| override file permission bits | `CAP_DAC_OVERRIDE` |
| bind port 80/443 | `CAP_NET_BIND_SERVICE` |
| use `ping` (raw ICMP) | `CAP_NET_RAW` (when present) |
| `chroot()` | `CAP_SYS_CHROOT` |
| create device nodes in its own `/dev` | `CAP_MKNOD` (but the devices cgroup and `nodev` still gate use) |
| switch to another UID and back | `CAP_SETUID`, `CAP_SETGID` |

| A container's root process **cannot**... | Because it lacks... |
|---|---|
| load or unload kernel modules | `CAP_SYS_MODULE` |
| change the system clock (`CLOCK_REALTIME`) | `CAP_SYS_TIME` |
| reboot the host | `CAP_SYS_BOOT` |
| mount most filesystems, `pivot_root`, `sethostname` after start | `CAP_SYS_ADMIN` |
| reconfigure host network interfaces or firewall | `CAP_NET_ADMIN` |
| `ptrace` arbitrary host processes | `CAP_SYS_PTRACE` (and the PID namespace hides them) |
| read the kernel log | `CAP_SYSLOG` |
| access raw I/O ports or `/dev/mem` | `CAP_SYS_RAWIO` |

Even the capabilities it **has** are constrained by the namespaces (Chapter 03)
and cgroups (Chapter 04): `CAP_NET_ADMIN` would only affect the container's own
network namespace; `CAP_MKNOD` cannot make a usable disk device because the
device cgroup denies it and `/dev` is `nodev`; `CAP_SYS_TIME` is not even present,
and there is no time namespace for `CLOCK_REALTIME` anyway.

## Four layers, one boundary

"Root in a container is confined" is the combined effect of four independent
mechanisms, each covered in this repository:

```text
                 a container's root process (UID 0)
                              │
   ┌──────────────┬──────────┴───────────┬─────────────────┐
   ▼              ▼                      ▼                 ▼
 namespaces    cgroups             capabilities        seccomp + LSM
 (Ch. 03)      (Ch. 04)            (Ch. 05)            (Ch. 06)
 can't SEE     can't OVERUSE       can't DO most        can't CALL
 host objects  resources           privileged ops       dangerous syscalls
```

Remove any one and the others still apply, which is defense in depth. But note
what is **shared**: the kernel. Every container calls into the same kernel code,
so a kernel vulnerability reachable through an allowed syscall can bypass all
four. This is the fundamental difference from a virtual machine, and the reason
for seccomp (reduce reachable syscalls) and user namespaces (make the attacker
unprivileged even if they escape).

## Where isolation can fail

Understanding the mechanisms also means knowing their edges:

- **`--privileged`.** Grants all capabilities, disables the seccomp and device
  filters, and mounts host `/dev`. A privileged container is essentially host
  root. Use it only when unavoidable, and never for untrusted workloads.
- **Individual dangerous capabilities.** `CAP_SYS_ADMIN`, `CAP_SYS_MODULE`,
  `CAP_SYS_PTRACE`, `CAP_DAC_READ_SEARCH`, `CAP_BPF`, and `CAP_NET_ADMIN` each
  enable known escape or host-impact techniques. Adding one with `--cap-add`
  can undo most of the confinement.
- **Writable host bind mounts.** Because default containers run as real UID 0,
  a writable bind mount of a host path (especially `/`, `/etc`, `/var/run/docker.sock`,
  or `/proc`/`/sys` paths) grants host access through **ordinary file ownership**,
  no capability required (Chapter 01 §7, Chapter 02 §3). This is the most common
  real-world container escape, and it needs no kernel bug.
- **Leaked file descriptors.** An open descriptor to a host object bypasses the
  mount namespace and root filesystem (Chapter 01 §5; CVE-2024-21626).
- **Kernel vulnerabilities.** Reachable through allowed syscalls; mitigated, not
  removed, by seccomp.
- **Missing user namespace.** Without one, all of the above happen as host UID 0.
  With one, the same escape lands as an unmapped, unprivileged host user
  (section 4).

The single most effective hardening step, when the workload tolerates it, is to
**not run as root** and to **enable a user namespace**, so that neither
capabilities nor file ownership grant host power. Everything else in this
chapter explains why.

## Why this matters for containers

This section *is* the container-security core of the repository. When you reach
runc (Chapter 11), you will see the code that drops the bounding set, applies the
capability sets, changes the user, and installs seccomp, in the order from
section 3. When you configure a workload, the tables above tell you which
capability a feature needs, so you can grant exactly that instead of
`--privileged`.

## Evidence

Lab: [`lab-04-root-in-a-container`](../../labs/05-capabilities/lab-04-root-in-a-container/)

## Further Reading

- Docker documentation: [Runtime privilege and Linux capabilities](https://docs.docker.com/engine/containers/run/#runtime-privilege-and-linux-capabilities)
  — the exact default capability list and what `--cap-add`, `--cap-drop`, and
  `--privileged` do.
- moby source: [`oci/caps/defaults.go`](https://github.com/moby/moby/blob/master/oci/caps/defaults.go)
  — the default capability set in code (the authoritative list for the installed
  version).
- Kubernetes documentation: [Configure a Security Context for a Pod or Container](https://kubernetes.io/docs/tasks/configure-pod-container/security-context/)
  and [Pod Security Standards](https://kubernetes.io/docs/concepts/security/pod-security-standards/)
  — `capabilities`, `runAsNonRoot`, `allowPrivilegeEscalation`, and the
  Restricted profile that drops all capabilities.
- NCC Group, [Understanding and Hardening Linux Containers](https://www.nccgroup.com/us/research-blog/understanding-and-hardening-linux-containers/)
  (whitepaper) — a thorough, vendor-neutral analysis of container isolation and
  its failure modes, tying together namespaces, capabilities, seccomp, and LSMs.
- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html)
  — for each capability in the "dropped" list, the exact operations it guards.
