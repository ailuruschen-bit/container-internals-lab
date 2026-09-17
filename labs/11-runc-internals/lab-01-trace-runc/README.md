# Lab 01 — Trace runc and See the Mechanisms

## Goal

Produce evidence that `runc run` performs exactly the syscalls of Chapters
01–08, in the order Chapter 11 describes:

1. `clone`/`unshare`/`setns` for namespaces (nsexec, §2);
2. `mount` and `pivot_root` for the rootfs (§3);
3. `prctl(PR_CAPBSET_DROP)`, `capset`, `prctl(PR_SET_NO_NEW_PRIVS)`, `seccomp`,
   then `execve` (§4);
4. the transient `runc:[0/1/2]` stage processes (§2).

## Prerequisites

- Linux VM with `sudo`, `runc`, `strace`, and the OCI bundle from Chapter 10
  Lab 01 at `/tmp/bundle` (config.json + rootfs).
- Read: Chapter 11 [§2](../../../docs/11-runc-internals/02-nsexec.md) and
  [§4](../../../docs/11-runc-internals/04-finalize.md).

## Experiment

### Part A — The stage processes

Terminal 1:

```bash
cd /tmp/bundle
sudo runc run demo &          # if config has terminal:false; else use two terminals
```

Terminal 2, immediately:

```bash
ps -eo pid,comm | grep -E 'runc' | head
```

You are trying to catch the short-lived `runc:[1:CHILD]` / `runc:[2:INIT]`
processes. They vanish quickly; re-run if you miss them.

Clean up: `sudo runc delete --force demo 2>/dev/null`.

### Part B — Trace the whole run

```bash
cd /tmp/bundle
sudo strace -f -e trace=clone,clone3,unshare,setns,mount,pivot_root,prctl,seccomp,capset,execve \
     runc run demo 2>/tmp/runc.strace &
sleep 2; sudo runc delete --force demo 2>/dev/null
grep -nE 'unshare|setns|clone' /tmp/runc.strace | head
grep -nE 'pivot_root|mount\(' /tmp/runc.strace | head
grep -nE 'prctl\(PR_CAPBSET_DROP|PR_SET_NO_NEW_PRIVS|seccomp\(|capset' /tmp/runc.strace | head
grep -n 'execve' /tmp/runc.strace | tail
```

**Predict first.** In the trace, will `pivot_root` come before or after the
capability drops and `seccomp`? Will `execve` of the container command be first
or last?

### Part C — The created/running split

```bash
cd /tmp/bundle
sudo runc create demo2
sudo runc state demo2 | grep -o '"status":"[a-z]*"'
# The init is blocked on the exec FIFO now:
INIT=$(sudo runc state demo2 | sed 's/.*"pid":\([0-9]*\).*/\1/')
sudo cat /proc/$INIT/status | grep -E 'Seccomp|NoNewPrivs|CapBnd'
sudo cat /proc/$INIT/wchan; echo
sudo runc start demo2
sudo runc delete --force demo2
```

## Expected observations

**Part A.** You may catch processes named `runc:[1:CHILD]` and/or
`runc:[2:INIT]` (the nsexec stages, §2), plus the main `runc`.

**Part B.** The order in the trace is:
`unshare`/`clone` with `CLONE_NEW*` (and uid_map writes) → `mount(...)` several
times and `pivot_root(...)` → `prctl(PR_CAPBSET_DROP,...)` (several) →
`prctl(PR_SET_NO_NEW_PRIVS,1)` → `seccomp(SECCOMP_SET_MODE_FILTER,...)` →
`execve("/bin/sh", ...)` **last**. This is precisely Chapter 11 §4's order.

**Part C.** After `create`, `status` is `created`. The blocked init already shows
`Seccomp: 2`? — depending on runc version, seccomp may be installed just before
exec, so at `created` you may see `Seccomp: 0` and a reduced `CapBnd` and
`NoNewPrivs: 1`; `wchan` shows it sleeping in a read (the exec FIFO). After
`start`, it execs.

(Whether seccomp is visible at `created` vs only after `start` is version- and
config-dependent; note what your runc does — it is a good check of §4's ordering
claims against your version.)

## Why this happens

- nsexec creates the namespaces first (§2); the Go init does rootfs (§3) then
  privilege reduction and exec (§4). strace `-f` follows all stages.
- The `created` state is the init blocked on the exec FIFO (Chapter 10 §3,
  Chapter 11 §4).

## Connection to containers

- This is the ground truth for the entire repository: a production runtime
  issuing the exact syscalls you learned, in the exact order the mechanism
  chapters said was required.
- Chapter 12 will show containerd's shim invoking `runc create`/`start` like this,
  under the hood of `docker run`.

## Questions to think about

1. Map each distinct syscall in `/tmp/runc.strace` to a chapter of this
   repository.
2. Why does `execve` of the container command appear only once and last, despite
   runc itself re-exec'ing (`runc init`) earlier in the trace?
3. If you add `-userns` equivalent (uid/gid mappings) to `config.json`, what new
   syscalls/writes appear, and in which stage? (§2)
