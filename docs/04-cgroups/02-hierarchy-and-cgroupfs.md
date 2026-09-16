# 2. The cgroup v2 Hierarchy and Filesystem

## The interface is a filesystem

cgroups have almost no dedicated system calls. The whole interface is a
pseudo-filesystem of type `cgroup2` (Chapter 02 §5), normally mounted at
`/sys/fs/cgroup`:

```bash
$ findmnt /sys/fs/cgroup
TARGET         SOURCE  FSTYPE  OPTIONS
/sys/fs/cgroup cgroup2 cgroup2 rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot
```

The mapping is direct:

| Filesystem operation | cgroup operation |
|---|---|
| `mkdir /sys/fs/cgroup/app` | create a cgroup |
| `rmdir /sys/fs/cgroup/app` | remove a cgroup (only if it has no processes and no children) |
| `echo 4242 > app/cgroup.procs` | move process 4242 (all its threads) into `app` |
| `cat app/memory.current` | read a statistic |
| `echo 512M > app/memory.max` | set a limit |

Every setting is therefore a `write()` system call, and every statistic a
`read()`, which is why the lab can use nothing but `echo` and `cat`.

## The hierarchy

The root of the mount is the **root cgroup**. Every directory below it is a
cgroup. On a systemd machine the top of the tree looks like this:

```text
/sys/fs/cgroup                       root cgroup
├── init.scope                       PID 1 (systemd)
├── system.slice                     system services
│   ├── ssh.service
│   ├── containerd.service
│   └── docker-<id>.scope            a container, when Docker uses the systemd driver
├── user.slice                       login sessions
│   └── user-1000.slice
│       └── session-3.scope          your SSH session and its shell
└── kubepods.slice                   on a Kubernetes node
```

systemd names cgroups by unit type: **slices** group other units, **services**
contain daemons, **scopes** contain externally started processes.

## Membership

Four rules define which cgroup a process is in:

1. **Every process is in exactly one cgroup** of the v2 hierarchy.
2. **A child starts in its parent's cgroup** (`fork()`/`clone()`). This is what
   makes cgroups work for groups of processes that do not exist yet.
3. **A process moves** when its PID is written into another cgroup's
   `cgroup.procs`. All threads of the process move together. Already-charged
   memory is **not** moved with it; only new charges go to the new cgroup.
4. **Membership is preserved across `execve()`**.

A process's cgroup is visible in `/proc/<pid>/cgroup`:

```text
0::/user.slice/user-1000.slice/session-3.scope
│ │ └ path relative to the cgroup2 mount root (as seen by the reader, see §6)
│ └ controller list: empty for v2
└ hierarchy ID: always 0 for v2
```

Since Linux 5.7, `clone3()` with `CLONE_INTO_CGROUP` can create a child
**directly** inside a different cgroup, avoiding a window in which the child
runs in the parent's cgroup (Chapter 01 §3 showed the `cgroup` field of
`struct clone_args`).

## Interface files

Every cgroup directory contains **core files** (prefix `cgroup.`) and
**controller files** (prefix = controller name).

| Core file | Purpose |
|---|---|
| `cgroup.procs` | PIDs of member processes; write a PID to move a process |
| `cgroup.threads` | TIDs of member threads (for threaded subtrees) |
| `cgroup.controllers` | controllers **available** in this cgroup (read-only) |
| `cgroup.subtree_control` | controllers **enabled for the children** of this cgroup |
| `cgroup.events` | `populated 1/0` (has processes anywhere below), `frozen` |
| `cgroup.freeze` | write `1` to freeze all processes (Linux 5.2+) |
| `cgroup.kill` | write `1` to `SIGKILL` every process in the subtree (Linux 5.14+) |
| `cgroup.max.depth`, `cgroup.max.descendants` | limit the size of the subtree |
| `cgroup.stat` | number of descendant cgroups |

Controller files follow naming conventions:

| Pattern | Meaning | Example |
|---|---|---|
| `X.current` | current usage | `memory.current`, `pids.current` |
| `X.max` | hard limit | `memory.max`, `pids.max`, `cpu.max`, `io.max` |
| `X.high`, `X.low`, `X.min` | softer thresholds | `memory.high` |
| `X.weight` | proportional share | `cpu.weight`, `io.weight` |
| `X.stat` | detailed statistics | `memory.stat`, `cpu.stat`, `io.stat` |
| `X.events` | counters of notable events | `memory.events`, `pids.events` |
| `X.pressure` | pressure stall information (PSI) | `cpu.pressure`, `memory.pressure` |

## Enabling controllers: top-down

A controller does not automatically apply everywhere. In cgroup v2, controllers
are enabled **top-down**:

```text
/sys/fs/cgroup/cgroup.controllers        cpuset cpu io memory hugetlb pids rdma misc
/sys/fs/cgroup/cgroup.subtree_control    cpu io memory pids        ← enabled for root's children
/sys/fs/cgroup/app/cgroup.controllers    cpu io memory pids        ← therefore available in app
/sys/fs/cgroup/app/cgroup.subtree_control  memory pids             ← enabled for app's children
/sys/fs/cgroup/app/web/cgroup.controllers  memory pids             ← web gets memory.* and pids.* files
```

Writing `+memory` or `-memory` to `cgroup.subtree_control` enables or disables
a controller for all children. Only when a controller is available in a cgroup
do its interface files (`memory.max`, ...) appear there.

## The "no internal processes" rule

cgroup v2 imposes one structural rule that surprises everyone once:

> A non-root cgroup that **has controllers enabled in `cgroup.subtree_control`**
> cannot **also contain processes** directly. Processes must live in leaf
> cgroups.

```text
app/                 subtree_control: memory      ← may not contain processes
├── web/             cgroup.procs: 101 102        ← OK: a leaf
└── worker/          cgroup.procs: 201            ← OK: a leaf
```

The reason: if `app` contained processes *and* children, the memory controller
would have to compare the processes in `app` against its child cgroups, which
are different kinds of entities, and the resource distribution would be
ambiguous. Trying to break the rule fails with `EBUSY` (`Device or resource
busy`). The root cgroup is exempt.

Container runtimes follow this: a container's processes live in a leaf cgroup;
a pod-level cgroup contains only child cgroups.

## Limits are hierarchical

A limit on a cgroup applies to the **whole subtree** below it. If `app` has
`memory.max = 1G`, then `web` and `worker` together can never use more than 1 GiB,
whatever their own limits say. A child can have a *lower* limit than its parent,
never an effectively higher one. This is how Kubernetes nests limits: node
allocatable → QoS class → pod → container.

## Delegation

Writing to cgroup files requires permission on those files, like any other
file. A privileged manager (systemd, or containerd running as root) can
**delegate** a subtree to a less-privileged user by changing ownership of the
directory and of `cgroup.procs`, `cgroup.subtree_control`, and `cgroup.threads`.
The delegatee can then create sub-cgroups and move its own processes among them,
but cannot escape the subtree or change the limits set above it.

Two rules make delegation safe:

- to move a process, the writer needs write access to `cgroup.procs` of the
  **common ancestor** of the source and destination cgroups;
- limits are set in files of the **parent's** delegation boundary, which the
  delegatee does not own.

Rootless containers depend on delegation: systemd delegates a subtree under
`user@1000.service` to the user (`Delegate=yes`), and rootless runtimes create
container cgroups inside it.

## systemd and "who owns the tree"

On systemd hosts, systemd considers itself the owner of the cgroup hierarchy.
Other programs should either ask systemd to create cgroups (through D-Bus,
creating a transient **scope** unit) or work inside a delegated subtree.
This is why container runtimes offer two **cgroup drivers**:

| Driver | How the runtime creates a cgroup | Typical path |
|---|---|---|
| `cgroupfs` | `mkdir` and writes directly under `/sys/fs/cgroup` | `/sys/fs/cgroup/docker/<id>` |
| `systemd` | asks systemd to create a transient scope with properties such as `MemoryMax=` | `/sys/fs/cgroup/system.slice/docker-<id>.scope` |

Kubernetes recommends the `systemd` driver on systemd hosts, so that the
kubelet, the container runtime, and systemd share one view of the tree.

For quick experiments, `systemd-run --scope -p MemoryMax=256M <command>` creates
a cgroup and runs a command in it, which is the same thing a runtime using the
systemd driver does.

## Why this matters for containers

- A container's cgroup is a directory; its limits are files in it. From the
  host you can read `cat /proc/<container-pid>/cgroup` and then inspect the
  corresponding directory under `/sys/fs/cgroup`.
- The runtime must place the container process into its cgroup **before**
  `execve()` (or use `CLONE_INTO_CGROUP`), so the application can never run
  unlimited.
- `cgroup.kill` and `cgroup.freeze` implement "kill every process in the
  container" and `docker pause` reliably, without racing against newly forked
  processes.

## Evidence

Lab: [`lab-01-cgroupfs-basics`](../../labs/04-cgroups/lab-01-cgroupfs-basics/)

## Further Reading

- Kernel docs: [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html),
  sections "Basic Operations" (creating, organizing processes, enabling
  controllers, top-down constraint, no internal process constraint),
  "Delegation", and "Core Interface Files". The primary source for every table
  above.
- [`cgroups(7)`](https://man7.org/linux/man-pages/man7/cgroups.7.html),
  sections on cgroup v2 and `/proc/<pid>/cgroup`.
- systemd documentation: [Control Group APIs and Delegation](https://systemd.io/CGROUP_DELEGATION/)
  — how systemd expects other software (including container managers) to use
  the tree; explains slices, scopes, and `Delegate=`.
- [`systemd-run(1)`](https://www.freedesktop.org/software/systemd/man/latest/systemd-run.html)
  and [`systemd.resource-control(5)`](https://www.freedesktop.org/software/systemd/man/latest/systemd.resource-control.html)
  — the systemd properties (`MemoryMax=`, `CPUQuota=`, `TasksMax=`) and the
  cgroup files they write.
- Kubernetes documentation: [About cgroup v2](https://kubernetes.io/docs/concepts/architecture/cgroups/)
  and [Configuring a cgroup driver](https://kubernetes.io/docs/tasks/administer-cluster/kubeadm/configure-cgroup-driver/).
