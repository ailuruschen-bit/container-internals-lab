# Lab 02 — UTS Namespace: clone, unshare, setns

## Goal

Produce evidence that:

1. a new UTS namespace starts as a copy and then diverges;
2. only `nodename` and `domainname` are isolated, not the kernel release;
3. `clone(CLONE_NEWUTS)`, `unshare(CLONE_NEWUTS)`, and `setns()` are three ways
   to reach the same kind of namespace;
4. namespace membership survives `execve()`.

## Prerequisites

- Linux VM with `sudo`, `gcc`, `util-linux`, `strace`.
- Read: [2. UTS namespace](../../../docs/03-namespaces/02-uts-namespace.md).

## Experiment

### Part A — `unshare` in the shell

**Predict first.** Immediately after `unshare --uts`, before calling
`hostname NEW`, what will `hostname` print?

```bash
hostname
sudo unshare --uts bash -c 'echo "before: $(hostname)"; hostname container-1; echo "after: $(hostname)"; uname -r'
hostname; uname -r
```

### Part B — What `unshare` actually calls

```bash
sudo strace -f -e trace=unshare,sethostname,execve unshare --uts hostname container-2
```

### Part C — `clone()` in C

```bash
gcc -Wall -o uts_clone uts_clone.c
gcc -Wall -o ns_join ns_join.c
./uts_clone container-3; echo "exit code: $?"      # without root
sudo ./uts_clone container-3
```

While the child sleeps (60 seconds), open a **second terminal** and use the
child PID printed by the program:

```bash
CHILD=<child pid>
hostname
sudo ./ns_join /proc/$CHILD/ns/uts hostname
sudo ./ns_join /proc/$CHILD/ns/uts bash -c 'hostname; readlink /proc/self/ns/uts; hostname changed-from-join'
sudo ./ns_join /proc/$CHILD/ns/uts hostname
hostname
```

### Part D — Membership survives exec, and children inherit it

```bash
sudo unshare --uts bash -c 'hostname inherited; bash -c "sh -c \"hostname; readlink /proc/self/ns/uts\""'
readlink /proc/self/ns/uts
```

## Expected observations

**Part A.** `before:` prints the **host's** hostname (a copy), `after:` prints
`container-1`. `uname -r` is identical inside and outside. After the command,
the host's hostname is unchanged.

**Part B.**

```text
unshare(CLONE_NEWUTS)                   = 0
execve("/usr/bin/hostname", ["hostname", "container-2"], ...) = 0
sethostname("container-2", 11)          = 0
```

(The first `execve` of `unshare` itself also appears.) The order proves the
pattern: create namespace → `execve` → the new program acts inside it.

**Part C.** Without root: `clone (are you root?): Operation not permitted`.
With root:

```text
parent pid=5000 nodename=lab release=6.8.0-45-generic ns=uts:[4026531838]
parent created child pid=5001
child  pid=5001 nodename=container-3 release=6.8.0-45-generic ns=uts:[4026532301]
parent pid=5000 nodename=lab release=6.8.0-45-generic ns=uts:[4026531838]
```

In the second terminal, `ns_join ... hostname` prints `container-3`; after
`hostname changed-from-join` inside the joined shell, the next `ns_join` prints
`changed-from-join`; the host's `hostname` stays `lab` throughout.

**Part D.** The grandchild `sh` prints `inherited` and a UTS inode different
from the final host `readlink`.

## Why this happens

- A new `uts_namespace` is created by copying the creator's `new_utsname`
  structure (`copy_utsname()`), so it begins identical.
- `uname()` fills `release` and `version` from values that are the same for the
  whole kernel.
- `clone()` needs `CAP_SYS_ADMIN` for any `CLONE_NEW*` flag except
  `CLONE_NEWUSER`.
- `setns()` replaced `nsproxy->uts_ns` of the `ns_join` process; `execvp()`
  preserved it, and every child inherited it.

## Connection to containers

- `uts_clone.c` is the first building block of the mini container in
  Chapter 09: `clone()` with namespace flags, then configuration (here
  `sethostname`) in the child.
- `ns_join.c` is the core of `docker exec`, with one namespace instead of
  several.
- The unchanged `uname -r` is why software inside a container that checks the
  kernel version sees the host kernel.

## Questions to think about

1. `ns_join` calls `setns()` and then `execvp()`. Why is this order necessary,
   rather than executing first?
2. Why can `hostname` inside a container differ from the name that other
   machines use to reach it? Which namespace and which file determine each?
3. What would happen if `uts_clone.c` passed `CLONE_NEWUTS` without `SIGCHLD`?
   (Hint: Chapter 01 §2 and §3.)
4. Modify `ns_join.c` to pass `CLONE_NEWNET` as `nstype` and give it a UTS file.
   What error do you expect?
