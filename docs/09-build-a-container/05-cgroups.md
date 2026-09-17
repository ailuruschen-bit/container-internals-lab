# 5. cgroup Limits

## What we add

`cgroups.go` and a call in `parent.go` after the child starts:

```go
cg := newCgroup("minic-" + strconv.Itoa(cmd.Process.Pid))
cg.setup(cfg, cmd.Process.Pid)   // create dir, write limits, add the child
defer cg.cleanup()
```

`setup` (Chapter 04):

```go
os.WriteFile("/sys/fs/cgroup/cgroup.subtree_control", []byte("+memory +pids"), 0644)
os.Mkdir(c.root, 0755)                              // a new cgroup = a directory
c.write("memory.max", ...)                          // Ch. 04 §3
c.write("pids.max", ...)                            // Ch. 04 §5
c.write("cgroup.procs", strconv.Itoa(pid))          // move the container in
```

## Why the parent does this

The cgroup filesystem is on the host; the parent, which is in the host's cgroup
namespace and (usually) privileged, writes the limits and moves the child's PID
into the new cgroup. Because cgroup membership is **inherited across fork**
(Chapter 04 §2), every process the container later creates is automatically in
the same cgroup, so the limits cover the whole container.

`minic` writes the limits right after `Start()`. A production runtime uses a
sync pipe so the child blocks until the cgroup (and user-namespace maps) are
ready before it does anything; `minic`'s child setup is fast and its first
child processes appear only at `execve`, so the simplification is acceptable for
learning. The docs note this as a known shortcut.

## What changed

Run `sudo ./minic run -mem $((64*1024*1024)) -pids 64 -- /bin/sh`:

- `cat /proc/self/cgroup` shows the container in `minic-<pid>` (path relative to
  the host, since `minic` does not create a cgroup namespace — a documented
  simplification; Chapter 04 §6 explains what a cgroup namespace would change).
- Memory: allocating past 64 MiB triggers the cgroup OOM killer, and the
  offending process dies with `SIGKILL` (exit 137), exactly as in Chapter 04
  Lab 02.
- PIDs: a fork bomb inside stops at 64 tasks with `fork: Resource temporarily
  unavailable` and does not affect the host (Chapter 04 Lab 04).

## What has NOT changed

- **Privilege**: still real root, all capabilities, every syscall (§6 next).
- **Namespaces**: unchanged from §§2–4.
- Note the container can still *read* `/sys/fs/cgroup` if it were mounted inside;
  `minic` does not mount cgroupfs in the container, so the app cannot change its
  own limits — but a real runtime that mounts cgroupfs makes it read-only for
  this reason (Chapter 04 §6).

## Rootless caveat

With `-userns` (no sudo), writing under `/sys/fs/cgroup` usually fails unless
systemd has delegated a subtree to your user (Chapter 04 §2). `minic` catches the
error and logs "cgroup setup skipped" rather than failing, so the rootless demo
still runs, just without enforced limits. This is the real trade-off of rootless
containers, and why rootless runtimes rely on systemd delegation.

## Further Reading

- Chapter 04, especially [§2 hierarchy](../04-cgroups/02-hierarchy-and-cgroupfs.md),
  [§3 memory](../04-cgroups/03-memory-controller.md),
  [§5 pids](../04-cgroups/05-pids-and-io-controllers.md),
  [§6 cgroup namespace](../04-cgroups/06-cgroup-namespace.md).
- runc/opencontainers: [`cgroups`](https://github.com/opencontainers/cgroups) —
  the production cgroup manager (Chapter 11).
