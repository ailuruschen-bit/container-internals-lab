# References — Chapter 06: seccomp and no_new_privs

Version note: kernel source pinned to Linux **v6.12**; libseccomp behavior for
2.5+; the default profile description matches recent moby.

## Kernel documentation (primary)

| Document | Why read it | Used in |
|---|---|---|
| [Seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html) | The definitive reference: modes, `seccomp_data`, the architecture pitfall, all return actions, user notification, filter stacking. | §2–3 |
| [No New Privileges](https://docs.kernel.org/userspace-api/no_new_privs.html) | The exact semantics of the flag and its interaction with `execve()` and seccomp. | §1 |

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`seccomp(2)`](https://man7.org/linux/man-pages/man2/seccomp.2.html) | The syscall, flags, and action precedence. | §2–3 |
| [`seccomp_unotify(2)`](https://man7.org/linux/man-pages/man2/seccomp_unotify.2.html) | The user-notification protocol for `SCMP_ACT_NOTIFY`. | §3 |
| [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) | `PR_SET_NO_NEW_PRIVS`, `PR_SET_SECCOMP`, `PR_GET_SECCOMP`. | §1–2 |
| [`execve(2)`](https://man7.org/linux/man-pages/man2/execve.2.html) | When setuid and file capabilities are ignored (`no_new_privs`). | §1 |
| [`seccomp_init(3)`](https://man7.org/linux/man-pages/man3/seccomp_init.3.html), [`seccomp_rule_add(3)`](https://man7.org/linux/man-pages/man3/seccomp_rule_add.3.html) | The libseccomp API and `SCMP_CMP_*` comparators used in the labs. | §2–3 |
| [`setpriv(1)`](https://man7.org/linux/man-pages/man1/setpriv.1.html) | `--no-new-privs`, `--seccomp-filter`. | Labs |

## Kernel source (primary)

- [`kernel/seccomp.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/seccomp.c)
  — `__seccomp_filter()`, `seccomp_run_filters()`, and the action handling
  (`seccomp_do_user_notification`, kill paths). Readable and directly matches §2–3.
- [`include/uapi/linux/seccomp.h`](https://elixir.bootlin.com/linux/v6.12/source/include/uapi/linux/seccomp.h)
  — `struct seccomp_data`, the `SECCOMP_RET_*` action constants and their
  precedence.
- [`security/commoncap.c`](https://elixir.bootlin.com/linux/v6.12/source/security/commoncap.c)
  — where `no_new_privs` is honored during `execve()` credential calculation.

## Container runtimes and orchestration (for §4)

- Docker: [Seccomp security profiles for Docker](https://docs.docker.com/engine/security/seccomp/).
- moby: [`profiles/seccomp/default.json`](https://github.com/moby/moby/blob/master/profiles/seccomp/default.json)
  and [`profiles/seccomp/seccomp_default.go`](https://github.com/moby/moby/blob/master/profiles/seccomp/seccomp_default.go).
- OCI Runtime Specification: [config-linux.md — Seccomp](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#seccomp).
- Kubernetes: [Restrict a Container's Syscalls with seccomp](https://kubernetes.io/docs/tutorials/security/seccomp/),
  [Seccomp and Kubernetes](https://kubernetes.io/docs/reference/node/seccomp/).
- [`seccomp/libseccomp-golang`](https://github.com/seccomp/libseccomp-golang) —
  the Go binding runc uses (Chapter 11).

## LSMs (for the §4 note)

- [Docker AppArmor security profiles](https://docs.docker.com/engine/security/apparmor/).
- Red Hat, ["Introduction to using SELinux with container runtimes"](https://www.redhat.com/en/blog/introduction-using-selinux-container-runtimes).

## Secondary sources (selected)

- LWN, ["A seccomp overview"](https://lwn.net/Articles/656307/) (2015) — design
  narrative, including why filters cannot dereference pointers.
- NCC Group, [Understanding and Hardening Linux Containers](https://www.nccgroup.com/us/research-blog/understanding-and-hardening-linux-containers/)
  — how seccomp fits with the other layers.
