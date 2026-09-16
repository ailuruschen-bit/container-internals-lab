# References — Chapter 05: Capabilities

Version note: kernel source links are pinned to Linux **v6.12**. Capability
numbers and the default container set are for Linux 6.x and recent Docker/moby;
verify the exact set on your system with `capsh --decode`.

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html) | The central reference: the capability list, the five sets, file capabilities, the `execve()` transformation, the root and UID-change rules, securebits, and user-namespace interaction. This whole chapter is a guided reading of it. | §1–5 |
| [`user_namespaces(7)`](https://man7.org/linux/man-pages/man7/user_namespaces.7.html) | Capabilities within a user namespace; namespaced file capabilities. | §4 |
| [`credentials(7)`](https://man7.org/linux/man-pages/man7/credentials.7.html) | Where capabilities sit among UIDs/GIDs. | §1 |
| [`capget(2)`](https://man7.org/linux/man-pages/man2/capget.2.html) | The raw `capget`/`capset` interface. | §2 |
| [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) | `PR_CAPBSET_DROP/READ`, `PR_CAP_AMBIENT`, `PR_SET_KEEPCAPS`, `PR_SET_SECUREBITS`, `PR_SET_NO_NEW_PRIVS`. | §2, §3 |
| [`capsh(1)`](https://man7.org/linux/man-pages/man1/capsh.1.html), [`setpriv(1)`](https://man7.org/linux/man-pages/man1/setpriv.1.html) | The tools used in every lab; `setpriv` maps one-to-one to the rules. | Labs |
| [`setcap(8)`](https://man7.org/linux/man-pages/man8/setcap.8.html), [`getcap(8)`](https://man7.org/linux/man-pages/man8/getcap.8.html), [`cap_from_text(3)`](https://man7.org/linux/man-pages/man3/cap_from_text.3.html) | File capabilities and the `=ep` syntax. | §2 |

## Kernel source (primary)

- [`include/uapi/linux/capability.h`](https://elixir.bootlin.com/linux/v6.12/source/include/uapi/linux/capability.h)
  — every capability number with a comment on what it controls.
- [`kernel/capability.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/capability.c)
  — `capable()`, `ns_capable()`, `capget`/`capset`.
- [`security/commoncap.c`](https://elixir.bootlin.com/linux/v6.12/source/security/commoncap.c)
  — `cap_capable()` (the namespace walk, §4), `cap_bprm_creds_from_file()` (the
  `execve()` transformation and root rule, §3), `cap_emulate_setxuid()` (UID
  transitions, §3).

## Container runtimes and orchestration (for connections)

- Docker: [Runtime privilege and Linux capabilities](https://docs.docker.com/engine/containers/run/#runtime-privilege-and-linux-capabilities).
- moby source: [`oci/caps/defaults.go`](https://github.com/moby/moby/blob/master/oci/caps/defaults.go)
  — the default capability set in code.
- OCI Runtime Specification: [config.md — POSIX process capabilities](https://github.com/opencontainers/runtime-spec/blob/main/config.md#linux-process)
  — the five capability arrays in `config.json` (Chapter 10).
- [`moby/sys/capability`](https://github.com/moby/sys/tree/main/capability) —
  the Go library runc uses for `capget`/`capset` (Chapter 11).
- Kubernetes: [Configure a Security Context](https://kubernetes.io/docs/tasks/configure-pod-container/security-context/),
  [Pod Security Standards](https://kubernetes.io/docs/concepts/security/pod-security-standards/).

## Secondary sources (selected)

- Michael Kerrisk, LWN, ["CAP_SYS_ADMIN: the new root"](https://lwn.net/Articles/486306/)
  (2012) — why one capability became so powerful.
- LWN, ["Inheriting capabilities"](https://lwn.net/Articles/632520/) (2015) —
  the motivation and design of ambient capabilities.
- NCC Group, [Understanding and Hardening Linux Containers](https://www.nccgroup.com/us/research-blog/understanding-and-hardening-linux-containers/)
  — container isolation and its failure modes, capability by capability.
