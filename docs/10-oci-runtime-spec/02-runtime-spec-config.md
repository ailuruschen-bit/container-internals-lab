# 2. The Runtime Spec: config.json

## The bundle

An OCI **bundle** is a directory containing exactly two things:

```text
bundle/
├── config.json     the container's configuration (the runtime spec)
└── rootfs/         the root filesystem (Chapter 07/08)
```

That is the entire input to a low-level runtime. `runc run` (Chapter 11) takes a
bundle and produces a running container. You can generate a starter config with
`runc spec`, which writes a default `config.json` you can edit.

## config.json, section by section

Here is the mapping every reader of this repository should internalize: each
major section of `config.json` **is** a Linux mechanism from Chapters 01–08, and
`minic` set the same things in code.

### `process` — what to run and as whom

```json
"process": {
  "terminal": false,
  "user": { "uid": 0, "gid": 0, "additionalGids": [] },
  "args": ["/bin/sh"],
  "env": ["PATH=/usr/bin:/bin", "TERM=xterm"],
  "cwd": "/",
  "capabilities": { "bounding": [...], "effective": [...], "permitted": [...],
                    "inheritable": [...], "ambient": [...] },
  "rlimits": [ { "type": "RLIMIT_NOFILE", "hard": 1024, "soft": 1024 } ],
  "noNewPrivileges": true
}
```

| Field | Chapter | minic equivalent |
|---|---|---|
| `args`, `env`, `cwd` | 01 §3 (`execve` argv/envp) | `cfg.cmd`, `childEnv()` |
| `user` | 01 §7 (setresuid/setgid) | `-userns` maps / (real runtime: capset+setuid) |
| `capabilities` (five sets) | 05 §2–3 | `caps.go` (bounding subset) |
| `rlimits` | 01 §5 (`setrlimit`) | (not in minic) |
| `noNewPrivileges` | 06 §1 | `setNoNewPrivs()` |
| `terminal` | 02 §5 (pty/devpts) | (minic inherits stdio) |

### `root` — the rootfs

```json
"root": { "path": "rootfs", "readonly": false }
```

Chapter 07 §1 (rootfs) and §4 (`readonly` = remount / read-only). `minic`'s
`-rootfs`.

### `hostname` / `domainname`

Chapter 03 §2 (UTS namespace). `minic`'s `-hostname` and `Sethostname`.

### `mounts` — the filesystem assembly

```json
"mounts": [
  { "destination": "/proc", "type": "proc", "source": "proc" },
  { "destination": "/dev", "type": "tmpfs", "source": "tmpfs",
    "options": ["nosuid","strictatime","mode=755","size=65536k"] },
  { "destination": "/dev/pts", "type": "devpts", "source": "devpts",
    "options": ["nosuid","noexec","newinstance","ptmxmode=0666"] },
  { "destination": "/data", "type": "bind", "source": "/host/data",
    "options": ["rbind","ro"] }
]
```

Each entry is exactly the arguments of `mount(2)` (Chapter 02 §2): `destination`
= target, `type`, `source`, `options` = flags. This is Chapter 02 §5 (special
filesystems), Chapter 02 §3 (bind mounts / volumes), and Chapter 07 §4 (the
assembly). `minic`'s `setupRootfs` hard-codes a subset of this list.

### `linux` — the Linux-specific mechanisms

```json
"linux": {
  "namespaces": [ {"type":"pid"}, {"type":"mount"}, {"type":"uts"},
                  {"type":"ipc"}, {"type":"network"}, {"type":"cgroup"},
                  {"type":"user"} ],
  "uidMappings": [ {"containerID":0,"hostID":100000,"size":65536} ],
  "gidMappings": [ ... ],
  "resources": {
    "memory": { "limit": 536870912 },
    "cpu":    { "quota": 150000, "period": 100000, "shares": 1024 },
    "pids":   { "limit": 200 },
    "unified": { "memory.high": "400M" }
  },
  "devices": [ { "type":"c", "path":"/dev/fuse", "major":10, "minor":229 } ],
  "seccomp": { "defaultAction":"SCMP_ACT_ERRNO", "architectures":[...],
               "syscalls":[ {"names":["read","write",...],"action":"SCMP_ACT_ALLOW"} ] },
  "maskedPaths": ["/proc/kcore", ...],
  "readonlyPaths": ["/proc/sys", ...],
  "rootfsPropagation": "rslave"
}
```

| Field | Chapter | minic equivalent |
|---|---|---|
| `namespaces` | 03 (all) | `Cloneflags` |
| `uidMappings`/`gidMappings` | 03 §7 | `-userns` mappings |
| `resources.memory/cpu/pids/io` | 04 §3–5 | `cgroups.go` (`-mem`, `-pids`) |
| `resources.unified` | 04 §7 | (direct cgroup v2 files) |
| `devices` | 02 §5, 04 (device cgroup / eBPF) | `makeDevNodes` (partial) |
| `seccomp` | 06 | `seccomp.go` (deny list) |
| `maskedPaths`/`readonlyPaths` | 07 §4 | (not in minic) |
| `rootfsPropagation` | 02 §4, 03 §4 | `make-rprivate` |

A namespace entry with a `path` (e.g. `{"type":"network","path":"/run/netns/x"}`)
means **join an existing** namespace instead of creating one (Chapter 03 §1
`setns` vs `clone`), which is how a container joins a Kubernetes pod's shared
network namespace.

## The whole point

Print a real container's config and you will recognize every section:

```bash
runc spec                      # writes a default config.json
# or, from a running container's bundle, cat config.json
```

There is nothing in `config.json` that is not a Linux mechanism you have already
used by hand. The OCI's contribution is the **schema**, the **defaults**, and the
**agreement** that all runtimes read it the same way.

## Further Reading

- OCI Runtime Spec: [config.md](https://github.com/opencontainers/runtime-spec/blob/main/config.md)
  (process, root, mounts, hooks, user) and
  [config-linux.md](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md)
  (namespaces, resources/cgroups, devices, seccomp, masked/readonly paths,
  propagation). Read these with the tables above.
- [`runc spec`](https://github.com/opencontainers/runc/blob/main/man/runc-spec.8.md)
  — generate and inspect a default config.
- The mechanism chapters (01–08) for anything that is unclear; each row above
  points to one.
