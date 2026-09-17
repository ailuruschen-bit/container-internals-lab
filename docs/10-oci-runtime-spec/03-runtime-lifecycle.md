# 3. The Runtime Lifecycle

## Why a lifecycle, not just "run"

`minic` did one thing: run a command and wait. Real systems need more: start a
container now but launch its process later, query its state, send it a signal,
run setup/teardown code at defined moments, and clean up. The runtime spec
defines a **state machine** and a set of **operations**, so that a higher-level
manager (containerd, Chapter 12) can drive any conforming runtime the same way.

## The states

```text
        create              start
  ┌──────────────► created ────────► running ──────► stopped
  │  (namespaces,                     (the process    (process exited
bundle  cgroups, rootfs               is executing)    or was killed)
  │  set up; process                                        │
  │  NOT yet started)                                       │ delete
  └─────────────────────────────────────────────────────◄──┘
```

| State | Meaning |
|---|---|
| **creating** | the runtime is applying `config.json` |
| **created** | all setup done (namespaces, cgroups, rootfs), but the user process has **not** been `execve`'d yet |
| **running** | the process is executing |
| **stopped** | the process has exited |

The split between **created** and **running** is the important design choice. It
means all the expensive, privileged setup happens at `create`, and `start` is
just "let the process run". A manager can create many containers, then start them
precisely, and hooks (below) can run between the phases.

## The operations

A conforming runtime provides these (runc's CLI matches them):

| Operation | Does | runc command |
|---|---|---|
| **create** | apply the bundle: new namespaces, cgroups, rootfs, then **block** the process just before `execve` | `runc create <id>` |
| **start** | release the blocked process so it `execve`s the entrypoint | `runc start <id>` |
| **state** | report the container's status as JSON (state, pid, bundle) | `runc state <id>` |
| **kill** | send a signal to the container process | `runc kill <id> SIGTERM` |
| **delete** | remove a stopped container's resources (cgroup, state) | `runc delete <id>` |

`runc run` is a convenience that does `create` + `start`. The
create-then-block-then-start mechanism is implemented with a pipe: after setup,
the container's init process waits on a FIFO; `start` writes to it. (This is the
"sync pipe" `minic` omitted in Chapter 09 §5.)

## Hooks

The spec defines **hooks**: programs the runtime runs at defined moments, on the
host or in the container namespaces. They are how networking and other host-side
setup attach to a container without the low-level runtime knowing about them.

| Hook | Runs | Namespace | Typical use |
|---|---|---|---|
| `createRuntime` | during create, after namespaces exist | host | (newer, ordered hooks) |
| `createContainer` | during create | container mount ns | |
| `startContainer` | just before the entrypoint execs | container | |
| `prestart` (deprecated) | before start | host | **CNI network setup** (add veth to the net ns) |
| `poststart` | after start | host | notify a monitor |
| `poststop` | after delete | host | tear down networking |

The `prestart`/`createRuntime` hooks are exactly where a network is wired into
the namespace `minic` left isolated (Chapter 09 §4): the runtime creates the
network namespace, the hook (a CNI plugin) puts a veth in it.

## The manager's view

Putting states, operations, and hooks together, a manager like containerd runs:

```text
write bundle (config.json + rootfs)
runc create web1        → state: created   (namespaces/cgroups/rootfs ready; hooks fire)
runc start  web1        → state: running   (execve happens)
runc state  web1        → {status: running, pid: 4242, ...}
... container runs ...
runc kill   web1 TERM   → the process gets SIGTERM
runc delete web1        → state gone, cgroup removed, poststop hook fires
```

This is the contract Chapter 12 (containerd's shim calls runc) and Chapter 13
(Docker via containerd) build on.

## Why this matters

- The created/running split and the sync pipe are the "proper" version of what
  `minic` did informally, and they explain how a manager controls timing.
- Hooks are the seam between the OCI runtime (which only isolates) and networking
  (CNI) — resolving the "isolated but disconnected" state from Chapter 09 §4.
- Every one of these operations is a runc subcommand you will trace in Chapter 11.

## Evidence

Lab: [`lab-01-runc-lifecycle`](../../labs/10-oci-runtime-spec/lab-01-runc-lifecycle/)

## Further Reading

- OCI Runtime Spec: [runtime.md](https://github.com/opencontainers/runtime-spec/blob/main/runtime.md)
  (the state machine and operations) and
  [config.md — POSIX-platform Hooks](https://github.com/opencontainers/runtime-spec/blob/main/config.md#posix-platform-hooks).
- runc man pages: [`runc-create`](https://github.com/opencontainers/runc/blob/main/man/runc-create.8.md),
  [`runc-start`](https://github.com/opencontainers/runc/blob/main/man/runc-start.8.md),
  [`runc-state`](https://github.com/opencontainers/runc/blob/main/man/runc-state.8.md),
  [`runc-kill`](https://github.com/opencontainers/runc/blob/main/man/runc-kill.8.md),
  [`runc-delete`](https://github.com/opencontainers/runc/blob/main/man/runc-delete.8.md).
- CNI [SPEC.md](https://github.com/containernetworking/cni/blob/main/SPEC.md) —
  invoked via hooks to wire the network namespace.
