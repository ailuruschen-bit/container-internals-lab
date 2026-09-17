# 1. The Re-exec Skeleton

## The problem: Go cannot configure the child in the gap

Chapter 01 §3 showed the ideal shape: `fork()`, configure the child in the gap,
then `execve()`. In C we write arbitrary code in that gap (Chapter 03's
`uts_clone.c` did). In Go this is not possible: `os/exec` gives no hook to run Go
code between clone and exec, the Go runtime is multithreaded, and Chapter 03 §4
noted that a multithreaded process cannot `setns()` into a mount namespace and
that `pivot_root` and user-namespace map writing have ordering constraints.

## The solution: run yourself again

The standard trick is **re-exec**: the parent starts a brand-new process that is
a copy of the same program (`/proc/self/exe`, the magic link from Chapter 01 §4),
passing a hidden first argument so the new process knows it is the "child". The
new process starts fresh **inside the new namespaces**, single-threaded at first,
and does the in-container setup as ordinary Go code before `execve()`-ing the
real command.

```text
minic run -- /bin/sh
   │  parent: choose namespaces, exec /proc/self/exe with "child"
   ▼
minic child /bin/sh   (a NEW process, already PID 1 in the new namespaces)
   │  child: sethostname, pivot_root, mount /proc, drop caps, seccomp
   ▼
/bin/sh               (execve: same PID, now the container's PID 1)
```

## In the code

`main.go` is the dispatcher:

```go
switch os.Args[1] {
case "run":                 // invoked by the user
    parent(os.Args[2:])
case "child":               // invoked by our own re-exec
    child(os.Args[2:])
}
```

`parent.go` builds the child command with `/proc/self/exe`:

```go
cmd := exec.Command("/proc/self/exe", append([]string{"child"}, cfg.cmd...)...)
cmd.SysProcAttr = &syscall.SysProcAttr{ Cloneflags: ... }
cmd.Start()
```

The namespace flags go in `SysProcAttr.Cloneflags` (Chapter 03 §8 showed this
maps to the raw `clone` syscall). Because they are on the `clone` that creates
the re-exec'd process, that process is born inside the new namespaces.

`child.go` is the code that runs "in the gap", except it is a whole process, so
it can be ordinary Go:

```go
func child(cmd []string) {
    syscall.Sethostname(...)   // §2
    setupRootfs(...)           // §3
    dropCapabilities(); setNoNewPrivs(); installSeccomp()  // §6
    syscall.Exec(path, cmd, os.Environ())   // the container starts
}
```

## What changed at this step

Nothing about isolation yet: with no `CLONE_NEW*` flags, `minic child` runs in
the host's namespaces. The skeleton just establishes the parent/child structure.
Run it with all flags off and it behaves like a slightly awkward `sh`.

## What has NOT changed

Everything: same hostname, same PIDs, same mounts, same root, same everything.
This is the baseline. The following sections each add one flag or one setup step
and ask again: what changed, what did not.

## How real runtimes do this

- runc uses a small **C** bootstrap called `nsexec` that runs before the Go
  runtime starts, precisely to avoid the multithreading constraints; it does the
  `clone`/`unshare`/`setns` and user-namespace map coordination in C, then hands
  off to Go. Chapter 11 reads it.
- The re-exec pattern itself is common in Go tooling (`containerd`'s shim,
  `nsenter`-like helpers). Seeing it here makes runc's `nsexec` less surprising.

## Further Reading

- Go: [`os/exec`](https://pkg.go.dev/os/exec) and
  [`syscall.SysProcAttr`](https://pkg.go.dev/syscall#SysProcAttr) — the fields
  used by `parent.go`.
- [`proc(5)`](https://man7.org/linux/man-pages/man5/proc.5.html), `/proc/self/exe`
  — the magic link that lets a program re-exec itself reliably.
- runc: [`libcontainer/nsenter`](https://github.com/opencontainers/runc/tree/main/libcontainer/nsenter)
  — the C bootstrap; the motivation is exactly this section's problem (Ch. 11).
