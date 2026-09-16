# Container Internals Lab

A learning repository that explains container technology **from first principles**:
starting with ordinary Linux processes and kernel mechanisms, and ending in the
source code of `runc`, `containerd`, and Moby (Docker Engine).

This is **not** a Docker tutorial. Docker commands appear only near the end,
as an application of mechanisms you will already understand.

## The question this repository answers

> What actually happens when I execute `docker run nginx`?

By the end you should be able to trace that command through every layer:

```text
docker CLI
    ↓   HTTP request over a Unix socket
Docker Engine / Moby
    ↓   gRPC
containerd
    ↓   starts a shim, which executes an OCI runtime
OCI runtime / runc
    ↓   clone / unshare / setns / mount / pivot_root / write to cgroupfs / prctl / seccomp / execve
Linux system calls
    ↓
Linux kernel
```

And you should be able to explain what the kernel did to the resulting
process:

```text
Linux process
    ├── namespaces   PID, mount, network, UTS, IPC, user, cgroup
    ├── cgroups      CPU, memory, PIDs, I/O
    ├── filesystem   rootfs, mounts, pivot_root, OverlayFS
    └── security     capabilities, seccomp, no_new_privs
```

The central idea that every chapter reinforces:

> **A container is still a Linux process.** The kernel gives that process an
> isolated and restricted *view* of system resources. Everything else is
> tooling that prepares and manages that process.

---

## How every concept is taught

Each major concept follows the same progression, so you never meet a Docker
abstraction before the Linux mechanism behind it:

```text
Problem → Normal Linux behavior → Linux abstraction → Kernel API
        → Small experiment → Observable evidence
        → How containers use it → How runc / containerd / Moby use it
```

Labs follow **prediction → experiment → observation → explanation**. Many
labs ask you to write down a prediction before running a command. Do it —
a wrong prediction is the fastest way to find a gap in your mental model.

---

## Learning dependency map

```text
                 Linux process model  (01)
                          ↓
            system calls, /proc, credentials
                          ↓
             mounts, VFS, root filesystem  (02)
                          ↓
                ┌─────────┴─────────┐
                ↓                   ↓
          namespaces (03)       cgroups v2 (04)
                ↓                   ↓
            isolation        resource control
                │                   │
                └─────────┬─────────┘
                          ↓
     security: capabilities (05), seccomp + no_new_privs (06)
     filesystem: rootfs / chroot / pivot_root (07), OverlayFS (08)
                          ↓
            build a mini container in Go  (09)
                          ↓
                OCI runtime spec  (10)
                          ↓
                     runc  (11)
                          ↓
                  containerd  (12)
                          ↓
              Moby / Docker Engine  (13)
```

### Why this order?

- **Processes first.** Namespaces are created by `clone()` or `unshare()`
  and joined by `setns()`. If `fork()`, `execve()`, and `clone()` are unclear,
  namespace creation looks like magic. Chapter 01 removes the magic.
- **Filesystem before namespaces.** The mount namespace is the first
  namespace ever added to Linux and the hardest to understand. It isolates the
  *mount table*, so you must first know what a mount table is (Chapter 02).
- **Namespaces and cgroups are independent.** Namespaces change what a process
  can *see*; cgroups change how much it can *use*. They can be learned in
  either order, but both are needed before security, because capabilities
  interact with the user namespace.
- **Capabilities before seccomp.** Capabilities restrict *privileged
  operations*; seccomp restricts *which system calls* can be made at all.
  `no_new_privs` connects the two.
- **rootfs / pivot_root after namespaces and capabilities.** `pivot_root`
  only makes sense inside a private mount namespace and requires
  `CAP_SYS_ADMIN`.
- **OverlayFS before building a container** so image layers are just a
  filesystem you have already mounted by hand.
- **Build it yourself before reading specs or runtime code.** When you later
  open an OCI `config.json` or runc's source, every section maps to something
  you have already implemented.
- **Top of the stack last.** containerd and Moby mostly *manage* containers
  (images, snapshots, lifecycle, APIs). Their design only makes sense once the
  thing being managed is understood.

Chapter numbering is a guide, not a contract. If a prerequisite turns out to be
missing, the curriculum will be reorganized instead of papering over the gap.

---

## Chapters

Difficulty scale: ★☆☆☆☆ (gentle) to ★★★★★ (demanding).

### 01 — Linux process fundamentals
[docs](docs/01-linux-process/) · [labs](labs/01-linux-process/)

- **What you will learn:** what a process is to the kernel; the path from
  application code through libc to system calls; PID/PPID and the process
  tree; `fork()`, `execve()`, `clone()` and how they differ; `/proc`; file
  descriptors; environment and arguments; signals; UID/GID and process
  credentials; PID 1 and orphan reaping.
- **Why it matters:** every container is a process created by these exact
  system calls. Namespaces are literally flags passed to `clone()`.
- **Prerequisites:** basic shell usage; ability to read small C programs
  (explained inline).
- **Difficulty:** ★★☆☆☆
- **Primary reading:** `man 2 fork`, `man 2 execve`, `man 2 clone`,
  `man 5 proc`, `man 7 credentials`, `man 7 signal`.

### 02 — Linux filesystem: VFS, mounts, root filesystem
[docs](docs/02-linux-filesystem/) · [labs](labs/02-linux-filesystem/)

- **What you will learn:** the Virtual File System (VFS) layer; inodes and
  dentries at a conceptual level; what "mounting" does; the mount table
  (`/proc/self/mountinfo`); bind mounts; mount propagation (shared, private,
  slave); the process root directory and working directory.
- **Why it matters:** container filesystems, volumes, and the mount namespace
  are all built on mounts. Mount propagation is a frequent source of
  surprising container behavior.
- **Prerequisites:** 01.
- **Difficulty:** ★★★☆☆
- **Primary reading:** kernel docs `filesystems/vfs.rst` and
  `filesystems/sharedsubtree.rst`; `man 2 mount`, `man 7 mount_namespaces`.

### 03 — Namespaces
[docs](docs/03-namespaces/) · [labs](labs/03-namespaces/)

- **What you will learn:** which global resources Linux had before
  isolation; the UTS, PID, mount, IPC, network, and user namespaces (the cgroup
  namespace is taught at the end of Chapter 04, after cgroups);
  `clone()`, `unshare()`, `setns()`; `/proc/<pid>/ns`; the tools `unshare`,
  `nsenter`, `lsns`.
- **Why it matters:** namespaces are what make a process believe it is alone
  on the machine.
- **Prerequisites:** 01, 02.
- **Difficulty:** ★★★★☆
- **Primary reading:** `man 7 namespaces` and each `*_namespaces(7)` page;
  Michael Kerrisk's LWN series "Namespaces in operation".

### 04 — cgroups v2
[docs](docs/04-cgroups/) · [labs](labs/04-cgroups/)

- **What you will learn:** why namespaces cannot limit resources; the unified
  cgroup v2 hierarchy; controllers (`cpu`, `memory`, `pids`, `io`);
  `/sys/fs/cgroup`; moving processes between cgroups; accounting vs limiting;
  the memory OOM killer inside a cgroup; the cgroup namespace.
- **Why it matters:** `--memory` and `--cpus` are just writes to files in
  cgroupfs. The JVM reads those same files to size its heap and thread pools.
- **Prerequisites:** 01, 02.
- **Difficulty:** ★★★☆☆
- **Primary reading:** kernel docs `admin-guide/cgroup-v2.rst`; `man 7 cgroups`.

### 05 — Capabilities and user namespaces
[docs](docs/05-capabilities/) · [labs](labs/05-capabilities/)

- **What you will learn:** traditional all-or-nothing root; the split into
  capabilities; the permitted, effective, inheritable, bounding, and ambient
  sets; how capabilities change across `execve()`; how user namespaces map
  IDs and grant capabilities *relative to a namespace*.
- **Why it matters:** explains why "root inside a container" is not
  automatically host root, and where that boundary can fail.
- **Prerequisites:** 01 (credentials), 03 (user namespace basics).
- **Difficulty:** ★★★★☆
- **Primary reading:** `man 7 capabilities`, `man 7 user_namespaces`.

### 06 — seccomp and no_new_privs
[docs](docs/06-seccomp/) · [labs](labs/06-seccomp/)

- **What you will learn:** seccomp strict and filter modes; classic BPF
  filters; filter actions (`ERRNO`, `KILL`, `TRAP`, `LOG`, `USER_NOTIF`);
  `PR_SET_NO_NEW_PRIVS` and why installing a filter usually requires it.
- **Why it matters:** seccomp reduces the kernel attack surface a container
  can reach, independent of namespaces and capabilities.
- **Prerequisites:** 01 (system calls), 05.
- **Difficulty:** ★★★★☆
- **Primary reading:** kernel docs `userspace-api/seccomp_filter.rst`;
  `man 2 seccomp`, `man 2 prctl`.

### 07 — rootfs, chroot, and pivot_root
[docs](docs/07-rootfs-chroot-pivot-root/)

- **What you will learn:** what a root filesystem contains; `chroot()` and
  why it is not a security boundary; `pivot_root()` inside a mount namespace;
  mounting `/proc`, `/dev`, and `/sys` for a new root.
- **Why it matters:** this is how a container gets "its own" `/`.
- **Prerequisites:** 02, 03 (mount namespace), 05.
- **Difficulty:** ★★★☆☆
- **Primary reading:** `man 2 chroot`, `man 2 pivot_root`.

### 08 — OverlayFS
[docs](docs/08-overlayfs/)

- **What you will learn:** union filesystems; `lowerdir`, `upperdir`,
  `workdir`, `merged`; copy-up; whiteouts and opaque directories; building an
  overlay by hand and watching where writes land.
- **Why it matters:** image layers are read-only lower directories; the
  container's writable layer is the upper directory.
- **Prerequisites:** 02, 07.
- **Difficulty:** ★★★☆☆
- **Primary reading:** kernel docs `filesystems/overlayfs.rst`.

### 09 — Build a container manually
[docs](docs/09-build-a-container/)

- **What you will learn:** starting from a normal process, add UTS, PID,
  mount, and network namespaces, an isolated rootfs, `pivot_root`, cgroup
  limits, capability restrictions, and seccomp — one step at a time. Each step
  states *what changed* and *what has not changed*. Final version in Go.
- **Why it matters:** this is the synthesis of Chapters 01–08, and a direct
  preview of what runc does.
- **Prerequisites:** 01–08.
- **Difficulty:** ★★★★☆
- **Primary reading:** Go `syscall.SysProcAttr` and `golang.org/x/sys/unix`
  documentation; the man pages from earlier chapters.

### 10 — OCI specifications
[docs](docs/10-oci-runtime-spec/)

- **What you will learn:** why the OCI exists; the Image, Runtime, and
  Distribution specifications; the OCI bundle; `config.json` section by
  section mapped to Linux primitives; the runtime lifecycle
  (`create`, `start`, `kill`, `delete`) and hooks.
- **Why it matters:** the OCI Runtime Spec is the contract between high-level
  managers (containerd) and low-level runtimes (runc, crun, youki).
- **Prerequisites:** 09.
- **Difficulty:** ★★★☆☆
- **Primary reading:** `opencontainers/runtime-spec` (`config.md`,
  `config-linux.md`, `runtime.md`); `opencontainers/image-spec`.

### 11 — runc internals
[docs](docs/11-runc-internals/)

- **What you will learn:** a guided trace of `runc run` through
  `libcontainer`: the `nsexec` C bootstrap, namespace setup, rootfs and
  `pivot_root`, cgroup management, capabilities, seccomp, and the final
  `execve()`. Each step links to the earlier experiment it reproduces.
- **Why it matters:** this is where your manual container meets production
  code.
- **Prerequisites:** 09, 10; basic Go reading ability.
- **Difficulty:** ★★★★★
- **Primary reading:** `opencontainers/runc` (pinned release tag recorded in
  the chapter).

### 12 — containerd internals
[docs](docs/12-containerd-internals/)

- **What you will learn:** why a layer above runc is needed; content store,
  snapshotters, images, containers vs tasks, the runtime v2 shim, and one
  traced container creation path.
- **Why it matters:** containerd is the runtime used directly by Kubernetes
  (via CRI) and underneath Docker Engine.
- **Prerequisites:** 08, 10, 11.
- **Difficulty:** ★★★★☆
- **Primary reading:** `containerd/containerd` docs and source (pinned
  release tag recorded in the chapter).

### 13 — Moby / Docker Engine
[docs](docs/13-moby-docker-engine/)

- **What you will learn:** the Docker Engine API; how `dockerd` translates a
  request into containerd calls; networking and volume management at an
  architectural level; and a complete, layer-by-layer explanation of
  `docker run nginx`.
- **Why it matters:** it closes the loop back to the command you already know.
- **Prerequisites:** 12.
- **Difficulty:** ★★★★☆
- **Primary reading:** `moby/moby` source; Docker Engine API reference.

---

## Learning progress

A chapter is **Completed** only when its explanation is coherent,
prerequisites are covered, experiments are reproducible with documented
expected observations, important claims have authoritative references, and
the mechanism is connected back to containers. Files existing is not enough.

Chapters marked **Needs review** have complete content, but their labs have
not yet been run end to end on a Linux VM and checked against the documented
expected observations.

| Chapter | Topic | Status |
|---|---|---|
| 01 | Linux process fundamentals | Needs review |
| 02 | Linux filesystem | Needs review |
| 03 | Namespaces | Needs review |
| 04 | cgroups v2 | Needs review |
| 05 | Capabilities and user namespaces | Needs review |
| 06 | seccomp and no_new_privs | Needs review |
| 07 | rootfs, chroot, pivot_root | Not started |
| 08 | OverlayFS | Not started |
| 09 | Build a container manually | Not started |
| 10 | OCI specifications | Not started |
| 11 | runc internals | Not started |
| 12 | containerd internals | Not started |
| 13 | Moby / Docker Engine | Not started |

---

## Environment

Experiments require a **Linux** machine. macOS and Windows do not have
namespaces or cgroups; Docker Desktop hides a Linux VM from you.

Recommended setup:

- A disposable Linux VM (for example Ubuntu 24.04 or Debian 12) created with
  Lima, Multipass, UTM, or a cloud instance. Many later labs require root and
  modify kernel state, so do not use a machine you care about.
- Kernel 5.15 or newer, with cgroup v2 mounted at `/sys/fs/cgroup`.
- Packages: `build-essential`, `strace`, `procps`, `psmisc`, `util-linux`,
  `libcap2-bin`, `golang` (1.22+).

On an Apple Silicon Mac, for example:

```bash
brew install lima
limactl start --name=lab template://ubuntu-lts
limactl shell lab
sudo apt-get update && sudo apt-get install -y build-essential strace procps psmisc util-linux libcap2-bin golang
```

## Repository layout

```text
docs/        conceptual chapters (read these)
labs/        reproducible experiments for each chapter (do these)
examples/    larger reusable programs shared across chapters
references/  annotated primary sources: what to read and why
```

## Scope

In scope: Linux mechanisms, OCI, runc, containerd, Moby.
Out of scope: Docker command tutorials, Compose, Kubernetes architecture.
Connections to the JVM and backend engineering appear only where they clarify
a mechanism (for example, how the JVM reads cgroup limits).
