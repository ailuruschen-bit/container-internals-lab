# 8. Combining Namespaces

Sections 2–7 treated each namespace alone. A container uses several at once,
and they interact. This section explains the interactions that determine the
**order** in which a runtime must do things, how an existing set of namespaces
is **joined**, and what a combination of namespaces still does **not**
change.

## Interactions you have already seen

| Interaction | Why | Section |
|---|---|---|
| PID namespace needs a new mount namespace and a new `/proc` | procfs shows the PID namespace of its mounter | 3 |
| IPC namespace needs new `mqueue` and `/dev/shm` mounts | the mqueue fs follows its mounter; `/dev/shm` is just a tmpfs | 5 |
| Network namespace needs a new `/sys` mount to show its interfaces | sysfs net entries follow the mounter's network namespace | 6 |
| User namespace must exist **before** the others | the others are owned by it, which grants privilege over them | 7 |
| A mount namespace created with a user namespace gets slave propagation and locked mounts | a less-privileged namespace must not affect or uncover a more-privileged one | 4 |

## The order of container creation

Putting these constraints together gives the skeleton that every runtime
follows. Operations marked **parent** happen in the runtime process outside the
container; **child** operations happen inside the new process, in the gap before
`execve()`.

```text
parent: clone(CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS
              | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWCGROUP)
             (the kernel creates the user namespace first, then the others, owned by it)
   │
   ├─ child: wait on a pipe ◄────────────────────────────────────────────┐
   │                                                                     │
parent: write /proc/<child>/uid_map, setgroups, gid_map      (section 7) │
parent: create a veth pair, move one end into the child's net namespace  │
        (section 6); place the child in a cgroup (Chapter 04)             │
parent: signal the child to continue ────────────────────────────────────┘
   │
child: sethostname()                                         (section 2)
child: mount --make-rslave /; mount rootfs, /proc, /dev, /sys ... (sections 3–5, Chapter 02)
child: pivot_root() into the rootfs                         (Chapter 07)
child: configure lo/eth0 if not done by the parent          (section 6)
child: drop capabilities, set no_new_privs, install seccomp  (Chapters 05–06)
child: execve(application)                                   (Chapter 01 §3)
```

Two structural points:

- **Some setup is only possible from outside.** Writing ID maps requires a
  process in the parent user namespace; moving a veth end into the namespace
  needs a device that exists in the parent network namespace. So the child must
  **pause** until the parent finishes. Runtimes implement this with pipes or
  socket pairs, exactly as `unshare` and Go's `os/exec` do internally.
- **Some setup is only possible from inside.** `sethostname()` must be called by
  a process in the UTS namespace; `/proc` must be mounted by a process in the
  PID namespace.

## Joining an existing set of namespaces

`docker exec` or `nsenter --all` must enter **several** namespaces of an
existing process. The order matters:

1. **User namespace first** (if different): joining it gives the capabilities
   needed to join the other namespaces owned by it. `setns()` into a user
   namespace requires the caller to be single-threaded.
2. **Other namespaces** (IPC, UTS, network, cgroup, time).
3. **PID namespace**: `setns()` only changes `pid_ns_for_children`, so a
   `fork()` is still required afterwards.
4. **Mount namespace**, conventionally last, because it changes the root and
   working directory; after it, paths such as `/proc/<pid>/ns/*` from the host
   may no longer be reachable. Tools therefore **open all namespace files
   first**, then call `setns()` on the descriptors.

Since Linux 5.8, a process can instead open a **pidfd** of the target and call
`setns(pidfd, CLONE_NEWUSER | CLONE_NEWNS | ...)` once; the kernel joins all
requested namespaces atomically in the correct order.

```text
open all /proc/<pid>/ns/* → setns(user) → setns(ipc, uts, net, cgroup) → setns(pid) → setns(mnt)
→ fork() → child: execve(command)
```

## Namespaces from Go

The Go standard library can start a process in new namespaces directly, through
`syscall.SysProcAttr`:

```go
cmd := exec.Command("/bin/sh")
cmd.SysProcAttr = &syscall.SysProcAttr{
    Cloneflags: syscall.CLONE_NEWUSER | syscall.CLONE_NEWUTS | syscall.CLONE_NEWPID |
        syscall.CLONE_NEWNS | syscall.CLONE_NEWIPC | syscall.CLONE_NEWNET,
    UidMappings: []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getuid(), Size: 1}},
    GidMappings: []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getgid(), Size: 1}},
}
cmd.Run()
```

What this represents at the kernel level:

| Go field | Linux mechanism |
|---|---|
| `Cloneflags` | flags passed to the raw `clone` syscall in `syscall.forkAndExecInChild` |
| `UidMappings`, `GidMappings` | the **parent** writes `/proc/<child>/uid_map`, `setgroups`, `gid_map` while the child waits on a pipe |
| `GidMappingsEnableSetgroups: false` (default) | the parent writes `deny` to `/proc/<child>/setgroups` |
| `Unshareflags` | `unshare()` called in the child after `clone()` |
| `Credential` | `setgroups()`, `setgid()`, `setuid()` in the child |
| `cmd.Run()` | `clone` → (child) wait, `execve()`; (parent) `wait4()` |

Go hides the pipe synchronization, but it is the same parent/child
choreography shown above. The lab's `nsbox.go` uses exactly these fields and
lets you confirm each row with `strace`.

What Go's `os/exec` **cannot** do is let you run arbitrary Go code in the child
between `clone()` and `execve()`: the child side is a small, restricted function
inside the runtime. To mount `/proc`, call `pivot_root()`, or drop capabilities
in the gap, a Go runtime must either execute a helper program, or **re-execute
itself** (`/proc/self/exe`) with an argument that tells the new process to act
as the "child". Chapter 09 uses the re-exec pattern; runc uses a C bootstrap
(`nsexec`) for the same reason (Chapter 11).

## What has *not* changed

A process in new user, UTS, PID, mount, IPC, and network namespaces is still
very much a process on the host:

| Property | Changed by namespaces? |
|---|---|
| Hostname, PIDs, mount tree, IPC objects, network stack, UID mapping | **yes** |
| The kernel (`uname -r`), its bugs, and its global state | no |
| CPU, memory, I/O, and PID-count limits | no (Chapter 04) |
| The files it sees under `/` | no: the mount tree is a **copy** of the host's until the runtime mounts a new root and pivots into it (Chapters 07–08) |
| The cgroup it belongs to, and `/proc/self/cgroup` | no (Chapter 04, cgroup namespace) |
| Access to syscalls | no: every syscall remains available (Chapter 06) |
| Capabilities in the initial user namespace when the process is real root **without** a user namespace | no (Chapter 05) |
| Visibility of the process from the host (`ps`, `/proc/<host-pid>`) | no: the host always sees it |

The lab ends by checking each row of this table.

## Evidence

Lab: [`lab-08-combining-namespaces`](../../labs/03-namespaces/lab-08-combining-namespaces/)

## Further Reading

- [`setns(2)`](https://man7.org/linux/man-pages/man2/setns.2.html) — the pidfd
  form, and the per-type restrictions that dictate the join order.
- [`nsenter(1)`](https://man7.org/linux/man-pages/man1/nsenter.1.html) — the
  documented order in which `nsenter` enters namespaces, and the `--all` option.
- Go documentation: [`syscall.SysProcAttr`](https://pkg.go.dev/syscall#SysProcAttr)
  (Linux) — every field used in this section. Then read the implementation,
  [`src/syscall/exec_linux.go`](https://github.com/golang/go/blob/master/src/syscall/exec_linux.go),
  function `forkAndExecInChild1`, to see the raw `clone`, the map-writing pipe,
  and the order of operations in the child.
- Liz Rice, ["Containers From Scratch"](https://www.youtube.com/watch?v=8fi7uSYlOdc)
  (GOTO 2018, talk) — a live-coded Go container using these exact
  `SysProcAttr` fields and the re-exec trick; an excellent preview of
  Chapter 09.
- Michael Kerrisk, LWN, ["Namespaces in operation, part 7: network namespaces"](https://lwn.net/Articles/580893/)
  — closes the series with how namespaces combine in practice.
