# Lab 08 — Combining Namespaces, Joining Them, and What Has Not Changed

## Goal

Produce evidence that:

1. a Go program can create six namespaces at once, unprivileged, through
   `SysProcAttr`;
2. those fields become one `clone()` call plus parent-written ID maps;
3. an existing set of namespaces can be joined with `nsenter`;
4. several important properties of the process are **not** changed by
   namespaces: kernel, resource limits, files under `/`, cgroup membership,
   available syscalls, and host visibility.

## Prerequisites

- Linux VM with Go 1.22+, `strace`, `util-linux`, `iproute2`, `procps`.
- Unprivileged user namespaces enabled (see Lab 07's check).
- Read: [8. Combining namespaces](../../../docs/03-namespaces/08-combining-namespaces.md).

## Experiment

```bash
go build -o nsbox nsbox.go
```

### Part A — Six namespaces, no root

Terminal 1:

```bash
./nsbox /bin/bash
```

Inside (prompt `nsbox#`):

```bash
id
hostname box && hostname
mount -t proc proc /proc
ps -ef
ip -brief link
ipcs -m | tail -n +4
for ns in user uts pid mnt ipc net cgroup; do echo "$ns $(readlink /proc/self/ns/$ns)"; done
```

In terminal 2, on the host:

```bash
for ns in user uts pid mnt ipc net cgroup; do echo "$ns $(readlink /proc/self/ns/$ns)"; done
lsns -p "$(pgrep -n -x bash)"
```

**Predict first.** Which namespace inode will be the **same** inside and on
the host?

### Part B — What Go actually asked the kernel to do

Exit the box. Then:

```bash
strace -f -e trace=clone,clone3,execve,write,openat -e signal=none ./nsbox /bin/true 2>&1 \
  | grep -E 'clone|uid_map|gid_map|setgroups|execve|"(0 [0-9]+ 1|deny)'
```

### Part C — Join the box from outside

Terminal 1: `./nsbox /bin/bash`, then inside: `hostname joined-box; mount -t proc proc /proc; sleep 1000 &`.

Terminal 2 — the host PID is printed by `nsbox` on startup:

```bash
BOX=<host pid printed by nsbox>
sudo nsenter --target $BOX --all bash -c 'hostname; ps -ef; ip -brief link; id'
```

Now try the pidfd variant supported by recent `nsenter` (util-linux 2.39+), if
available, or read the order `nsenter` uses:

```bash
sudo strace -f -e trace=setns,openat,clone,clone3,execve nsenter --target $BOX --all true 2>&1 | grep -E 'ns/|setns|clone|execve'
```

### Part D — What has *not* changed

Inside the box (terminal 1):

```bash
uname -r                                  # kernel
nproc; free -m | head -n 2                # CPU and memory available
cat /proc/self/cgroup                     # cgroup membership
ls /home /etc | head                      # files under /
python3 -c 'import os; print(os.uname().release)' 2>/dev/null
cat /proc/self/status | grep -E '^(Seccomp|NoNewPrivs)'
```

On the host (terminal 2):

```bash
uname -r; nproc; free -m | head -n 2
cat /proc/$BOX/cgroup
ps -o pid,user,comm -p $BOX
```

Exit the box.

## Expected observations

**Part A.** Inside: `uid=0(root)`, hostname changes to `box`, `ps -ef` shows
only `bash` (PID 1) and `ps`, `ip link` shows only `lo` (down), `ipcs` is empty.
The `user`, `uts`, `pid`, `mnt`, `ipc`, and `net` inodes differ from the host;
**`cgroup` is identical**, because `nsbox` did not request `CLONE_NEWCGROUP`.
`lsns` on the host lists the six new namespaces for that bash process.

**Part B.** Output similar to:

```text
clone(child_stack=NULL, flags=CLONE_VFORK|CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWUSER|CLONE_NEWPID|CLONE_NEWNET|SIGCHLD) = 7001
openat(AT_FDCWD, "/proc/7001/uid_map", O_RDWR|O_CLOEXEC) = 5
write(5, "0 1000 1\n", 9)             = 9
openat(AT_FDCWD, "/proc/7001/setgroups", O_WRONLY|O_CLOEXEC) = 5
write(5, "deny", 4)                    = 4
openat(AT_FDCWD, "/proc/7001/gid_map", O_RDWR|O_CLOEXEC) = 5
write(5, "0 1000 1\n", 9)             = 9
[pid 7001] execve("/bin/true", ["/bin/true"], ...) = 0
```

The exact flag order and extra flags (for example `CLONE_VM`) depend on the Go
version, but **one** clone call carries all six `CLONE_NEW*` flags, and the
**parent** PID writes the maps before the **child** calls `execve`.

**Part C.** `nsenter --all` prints `joined-box`, the box's process list
(bash, sleep, ps), only `lo`, and `uid=0(root)`. The strace shows `nsenter`
opening all `/proc/<pid>/ns/*` files first, then a series of `setns()` calls,
then a `clone`/`fork` (needed for the PID namespace), then `execve`.

**Part D.** Inside and outside print the **same** kernel release, the same
number of CPUs and total memory, the **same** cgroup path, and the same files in
`/home` and `/etc`. `Seccomp: 0` and `NoNewPrivs: 0`. The host sees the box's
bash as a normal process owned by your user.

## Why this happens

- **A, B.** `syscall.forkAndExecInChild1` issues a raw `clone` with
  `SysProcAttr.Cloneflags`; the child blocks on a pipe while the parent writes
  the ID maps (`writeUidGidMappings` / `writeSetgroups`), then signals the child,
  which continues to `execve`.
- **C.** `nsenter` opens the namespace files of the target before joining any of
  them, joins the user namespace first, and forks after joining the PID
  namespace.
- **D.** Namespaces change *views* of specific resources. Resource limits,
  cgroup membership, the root filesystem contents, and syscall filtering are
  separate mechanisms, none of which `nsbox` configured.

## Connection to containers

- `nsbox` is roughly what the first 30 minutes of every "container from scratch"
  talk builds. Part D lists precisely what it is still missing compared with a
  real container; Chapters 04–08 add those pieces, and Chapter 09 assembles them.
- `nsenter --all` is `docker exec` without the container engine.

## Questions to think about

1. Add `syscall.CLONE_NEWCGROUP` to `Cloneflags`. What changes in
   `/proc/self/cgroup` inside the box? (Come back to this after Chapter 04.)
2. Why can `nsbox` mount `/proc` inside the box without leaking the mount to the
   host, even though `nsbox` never changes mount propagation? (Hint: section 4,
   "Less-privileged mount namespaces".)
3. Inside the box, `ip link set lo up` works, but the box still cannot reach
   the network. What would the parent process need to do, using Lab 06?
4. Why can `nsbox` not call `sethostname("box")` itself before running the
   command, using only `exec.Cmd`? What are the two ways around that?
