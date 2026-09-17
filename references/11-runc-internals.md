# References — Chapter 11: runc Internals

Version note: written against **runc v1.2.x**. Links point to the `main` branch;
function and struct names are stable across the series but **line numbers are
not**. Check `runc --version` and read the matching tag. Do not assume these
details describe older (0.x/1.0) or future runc unchanged.

## runc source (primary — this whole chapter is a reading of it)

| Path | What to read | Section |
|---|---|---|
| [`run.go`](https://github.com/opencontainers/runc/blob/main/run.go), [`create.go`](https://github.com/opencontainers/runc/blob/main/create.go), [`start.go`](https://github.com/opencontainers/runc/blob/main/start.go), [`init.go`](https://github.com/opencontainers/runc/blob/main/init.go) | the OCI CLI commands and the hidden `init` | §1 |
| [`libcontainer/process_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/process_linux.go) | `newParentProcess`, `initProcess.start()`, cgroup Apply/Set, the sync loop | §1, §4 |
| [`libcontainer/sync.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/sync.go) | the parent/child ordering protocol | §1 |
| [`libcontainer/init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/init_linux.go) | `StartInitialization`, init-type dispatch | §1 |
| [`libcontainer/nsenter/`](https://github.com/opencontainers/runc/tree/main/libcontainer/nsenter) | `nsexec.c`, `nsenter.go`, and the README | §2 |
| [`libcontainer/standard_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/standard_init_linux.go) | `Init()`, `finalizeNamespace` | §3, §4 |
| [`libcontainer/setns_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/setns_init_linux.go) | the `docker exec` join path | §1 |
| [`libcontainer/rootfs_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/rootfs_linux.go) | `prepareRootfs`, `mountToRootfs`, `pivotRoot`, `createDevices`, `maskPath`, `readonlyPath`, `finalizeRootfs` | §3 |
| [`libcontainer/capabilities/`](https://github.com/opencontainers/runc/tree/main/libcontainer/capabilities) | applying the five capability sets | §4 |
| [`libcontainer/seccomp/`](https://github.com/opencontainers/runc/tree/main/libcontainer/seccomp) | compiling the OCI seccomp profile via libseccomp | §4 |

## Dependencies runc uses (primary)

- [`opencontainers/cgroups`](https://github.com/opencontainers/cgroups) — the
  cgroup manager (`fs`, `fs2`, `systemd`); Chapter 04 in code (§4).
- [`seccomp/libseccomp-golang`](https://github.com/seccomp/libseccomp-golang) —
  the seccomp binding (§4).
- [`moby/sys/capability`](https://github.com/moby/sys/tree/main/capability) — the
  capability library (§4).

## Security advisories (primary for §5)

- runc [security advisories](https://github.com/opencontainers/runc/security/advisories)
  — CVE-2019-5736 (`/proc/self/exe`) and CVE-2024-21626 (fd leak,
  [GHSA-xr7r-f8xq-vfvv](https://github.com/opencontainers/runc/security/advisories/GHSA-xr7r-f8xq-vfvv)).
- Snyk, ["Leaky Vessels"](https://snyk.io/blog/leaky-vessels-docker-runc-container-breakout-vulnerabilities/)
  — CVE-2024-21626 walkthrough.

## Background

- runc [README](https://github.com/opencontainers/runc/blob/main/README.md) and
  [docs/](https://github.com/opencontainers/runc/tree/main/docs).
- The mechanism chapters (01–08) and Chapters 09 (`minic`) and 10 (OCI): every
  runc step maps to one of them.
