# Lab 05 — Inspecting Process State Through /proc

## Goal

Produce evidence that:

1. process tools such as `ps` get their information by reading `/proc`;
2. `/proc/self` refers to whichever process opens it;
3. `cmdline` and `environ` are the exact `argv` and `envp` given to `execve()`;
4. `exe`, `cwd`, and `root` are magic links to objects the process holds;
5. every process belongs to a set of namespaces and a cgroup, even when no
   container exists.

## Prerequisites

- Linux VM. `sudo` access is needed only for Part F.
- Read: [4. /proc](../../../docs/01-linux-process/04-proc-filesystem.md).

## Background

procfs generates file contents from kernel data structures at read time. This
lab also introduces `inspect_proc.sh`, a small script that prints the process
properties this repository cares about. You will reuse it in later chapters.

## Experiment

### Part A — `ps` reads files

```bash
strace -e trace=openat ps -o pid,comm -p $$ 2>&1 | grep "/proc/$$"
```

### Part B — Who is "self"?

**Predict first.** Will these two commands print the same PID?

```bash
echo $$
readlink /proc/self
```

Then:

```bash
grep -E '^(Name|Pid|PPid)' /proc/self/status
```

### Part C — `argv` and `envp` as the kernel stored them

```bash
env -i GREETING="hello world" python3 -c 'import time; time.sleep(300)' 'an argument with spaces' &
S=$!
sleep 0.5
cat -v /proc/$S/cmdline; echo
tr '\0' '\n' < /proc/$S/cmdline
echo ---
tr '\0' '\n' < /proc/$S/environ
kill $S
```

`env -i` starts the program with an **empty** environment plus the listed
variables. Python ignores the extra argument; it is there only so you can see
how an argument containing spaces is stored.

### Part D — Magic link: recover a deleted executable

```bash
cp /bin/sleep /tmp/mysleep
/tmp/mysleep 300 &
M=$!
rm /tmp/mysleep
ls -l /proc/$M/exe
cp /proc/$M/exe /tmp/recovered-sleep
/tmp/recovered-sleep 0.1 && echo "recovered binary works"
kill $M; rm /tmp/recovered-sleep
```

**Predict first.** After `rm`, can `cp /proc/$M/exe` still work?

### Part E — The full summary

```bash
chmod +x inspect_proc.sh
./inspect_proc.sh
./inspect_proc.sh 1
```

### Part F — Namespaces exist even without containers

```bash
readlink /proc/$$/ns/uts /proc/$$/ns/pid /proc/$$/ns/mnt
sudo readlink /proc/1/ns/uts /proc/1/ns/pid /proc/1/ns/mnt
```

## Expected observations

**Part A.** Lines such as
`openat(AT_FDCWD, "/proc/2201/stat", O_RDONLY) = 6` and
`openat(AT_FDCWD, "/proc/2201/status", O_RDONLY) = 6`.

**Part B.** Different numbers. `readlink /proc/self` prints the PID of the
`readlink` process, and `/proc/self/status` shows `Name: grep` with `PPid`
equal to your shell's PID.

**Part C.** `cat -v` shows the NUL separators as `^@`:

```text
python3^@-c^@import time; time.sleep(300)^@an argument with spaces^@
```

`environ` contains exactly one line: `GREETING=hello world`. No `PATH`, no
`HOME`.

**Part D.** `ls -l` shows `/proc/<pid>/exe -> /tmp/mysleep (deleted)`, yet the
copy succeeds and the recovered binary runs.

**Part E.** Without an argument, the script inspects the shell you ran it
from (its parent). For your shell: `Uid` values equal your user ID, several open file
descriptors pointing to `/dev/pts/N`, namespace links such as
`uts:[4026531838]`, a cgroup line starting with `0::/user.slice/...` on
systemd machines, `CapEff: 0000000000000000`, `Seccomp: 0`. For PID 1
without `sudo`: identity and cgroup are readable, while `exe`, `cwd`,
`root`, `fd`, and `ns` show `<permission denied>`.

**Part F.** The inode numbers in brackets are **identical** for your shell and
PID 1, for example `uts:[4026531838]` in both outputs.

## Why this happens

- **A, B.** procfs looks up the task when a path is opened. `self` is
  resolved against the task that performs the lookup.
- **C.** At `execve()`, the kernel copied `argv` and `envp` to the new stack and
  recorded their location in the process memory descriptor. procfs reads those
  bytes back.
- **D.** The process holds a reference to the executable file's inode. `rm`
  removes the directory entry (the name), but the file data stays alive while
  the reference exists. The magic link opens the referenced file directly.
- **E.** Reading another user's `fd/`, `exe`, `root`, or `ns/` requires
  ptrace-read permission on that process.
- **F.** Every process is always in exactly one namespace of each type. On a
  host without containers, almost all processes share the initial namespaces.
  The bracketed number is the inode number that identifies a namespace
  instance.

## Connection to containers

- In Chapter 03, Part F becomes the primary evidence: a process in a container
  shows **different** bracketed numbers for the namespaces the runtime
  created, and the same numbers for namespaces it did not.
- Part C is how you can check, from the host, exactly which environment a
  container process received.
- Part D's magic-link behavior is the basis of `/proc/<pid>/root`, which lets
  you browse a container's filesystem from the host. It has also been involved
  in real container escape vulnerabilities involving `/proc/self/exe`
  (for example CVE-2019-5736 in runc), which Chapter 11 discusses.

## Questions to think about

1. Why is `/proc/<pid>/environ` more restricted than `/proc/<pid>/cmdline`?
   What kind of data is commonly passed through environment variables in
   containerized applications?
2. In Part D, which command actually frees the disk space of the deleted file?
3. `ps` inside a container usually shows only the container's processes. Based
   on the "one important preview" paragraph, what two things must be true for
   that to happen?
4. Run `./inspect_proc.sh <jvm-pid>` on a running Java process. How many
   threads does it have, and which file descriptors point to `.jar` files?
