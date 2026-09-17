# 2. UTS and PID Namespaces

## What we add

Two clone flags in `parent.go`:

```go
flags := syscall.CLONE_NEWUTS | syscall.CLONE_NEWPID | ...
```

and, in `child.go`, one line that uses the UTS namespace:

```go
syscall.Sethostname([]byte(hostname))
```

## UTS: an isolated hostname

`CLONE_NEWUTS` gives the child its own hostname (Chapter 03 §2). Because the
child is in its own UTS namespace, `Sethostname` changes only the container's
name, not the host's.

## PID: the shell becomes PID 1

`CLONE_NEWPID` gives the child a new PID numbering space, and the re-exec'd
process becomes **PID 1** of it (Chapter 03 §3). This is why the re-exec pattern
matters: `CLONE_NEWPID` on `clone` puts the *new* process (not the parent) at
PID 1, which is exactly what we want, and avoids the `unshare(CLONE_NEWPID)`
"affects only children" subtlety from Chapter 03 §3.

## What changed

Run `sudo ./minic run -hostname box -- /bin/sh` (with the rootfs step from §3
still off, or pointing `-rootfs ""`):

- `hostname` prints `box`; the host's hostname is unchanged.
- `echo $$` prints `1`: the shell is PID 1 of its namespace.
- Signals now follow the PID 1 rules of Chapter 03 §3: a `SIGTERM` from the host
  with no handler is dropped; `SIGKILL` from the host still works and, when it
  kills PID 1, ends the whole namespace.

## What has NOT changed

- `ps` still shows **all host processes**, because `/proc` is still the host's
  procfs mounted for the host's PID namespace (Chapter 03 §3). PID isolation is
  real but **invisible** until §3 gives the container its own `/proc`. This is
  the single most instructive "what did not change" in the whole build.
- `ls /` still shows the host filesystem (no mount namespace setup yet).
- Same resources, privileges, and syscalls.

## Why the order will matter

We deliberately created `CLONE_NEWPID` here but cannot yet *see* its effect. That
is the motivation for §3: mounting a fresh `/proc` requires a mount namespace,
which requires the rootfs work. This dependency (PID namespace needs a new mount
namespace and `/proc` to be observable) is the concrete version of Chapter 03
§3's lesson.

## Further Reading

- Chapter 03 [§2 UTS](../03-namespaces/02-uts-namespace.md) and
  [§3 PID](../03-namespaces/03-pid-namespace.md) — the mechanisms this step uses.
- [`clone(2)`](https://man7.org/linux/man-pages/man2/clone.2.html),
  `CLONE_NEWUTS` and `CLONE_NEWPID`.
