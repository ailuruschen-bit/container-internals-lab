# Lab 04 — clone Flags: Copy or Share

## Goal

Produce evidence that:

1. `fork()` is `clone()` without sharing flags;
2. `CLONE_VM`, `CLONE_FS`, and `CLONE_FILES` decide whether memory, working
   directory, and file descriptor table are shared or copied;
3. threads are tasks created with sharing flags, visible under
   `/proc/<pid>/task/`.

## Prerequisites

- Linux VM with `gcc`, `strace`, and `python3`.
- Read: [3. fork, execve, and clone](../../../docs/01-linux-process/03-fork-exec-clone.md),
  especially "clone(): the general mechanism".

No root access is needed.

## Background

`copy_process()` in the kernel asks, for each resource, "share or copy?" The
`CLONE_*` flags provide the answer. `clone_flags.c` runs one child function
twice: once with only `SIGCHLD` (like `fork`), once with
`CLONE_VM | CLONE_FS | CLONE_FILES | SIGCHLD`.

The child changes a global variable, changes its working directory to `/`,
and opens a file. It returns the new file descriptor number as its exit
status. The parent then checks whether it can observe each change.

## Experiment

### Part A — Copy vs share

**Predict first.** Fill in this table before running anything:

| Mode | `counter` after child | parent `cwd` after child | child's fd open in parent? |
|---|---|---|---|
| `copy` | ? | ? | ? |
| `share` | ? | ? | ? |

```bash
gcc -Wall -o clone_flags clone_flags.c
./clone_flags copy
./clone_flags share
```

### Part B — Confirm the flags at the syscall level

```bash
strace -f -e trace=clone,clone3,chdir,openat ./clone_flags copy  2>&1 | grep -E 'clone|chdir|hostname'
strace -f -e trace=clone,clone3,chdir,openat ./clone_flags share 2>&1 | grep -E 'clone|chdir|hostname'
```

### Part C — Threads are tasks

```bash
python3 -c '
import threading, time
for _ in range(3):
    threading.Thread(target=time.sleep, args=(60,)).start()
time.sleep(60)' &
PY=$!
sleep 1
ls /proc/$PY/task
grep -E '^(Tgid|Pid|Threads)' /proc/$PY/status
for t in /proc/$PY/task/*; do grep -E '^(Tgid|Pid):' $t/status | tr '\n' ' '; echo; done
```

Then see how the threads were created:

```bash
kill $PY
strace -f -e trace=clone,clone3 python3 -c 'import threading; threading.Thread(target=lambda: None).start()' 2>&1 | grep clone
```

## Expected observations

**Part A.**

```text
mode=copy  parent pid=6000
before: counter=0 cwd=/tmp
child pid=6001 opened fd 3
after:  counter=0 cwd=/tmp fd 3 in parent: not open (EBADF)
```

```text
mode=share  parent pid=6010
before: counter=0 cwd=/tmp
child pid=6011 opened fd 3
after:  counter=42 cwd=/ fd 3 in parent: OPEN
```

**Part B.** The `copy` run shows `flags=SIGCHLD`; the `share` run shows
`flags=CLONE_VM|CLONE_FS|CLONE_FILES|SIGCHLD`. Both show the child's
`chdir("/")` and `openat(... "/etc/hostname" ...) = 3` with a `[pid N]` prefix.

**Part C.** `/proc/$PY/task` lists four entries: the main thread and three
more. Every task shows the **same** `Tgid` (equal to `$PY`), but a different
`Pid` (the TID). `Threads: 4`. The strace output shows a call such as:

```text
clone3({flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, ...}, 88) = 7002
```

Older glibc versions show `clone(...)` with the same flags.

## Why this happens

- Without `CLONE_VM`, the child gets a copy-on-write copy of memory, so
  `counter = 42` modifies the child's private page.
- Without `CLONE_FS`, the child has its own copy of root directory and working
  directory, so `chdir("/")` does not affect the parent.
- Without `CLONE_FILES`, the child has its own copy of the file descriptor
  table. The new fd 3 exists only in the child's table and was closed when the
  child exited.
- With the flags, both tasks point to the *same* kernel structures
  (`mm_struct`, `fs_struct`, `files_struct`), which is why every change is
  visible to the parent.
- `pthread_create()` uses the full set of sharing flags plus `CLONE_THREAD`,
  which puts the new task into the caller's thread group: same `Tgid` (the
  userspace PID), new `Pid` (the kernel task ID, userspace TID).

## Connection to containers

- In Chapter 03, you will add `CLONE_NEWUTS`, `CLONE_NEWPID`, and other
  `CLONE_NEW*` flags to exactly this kind of `clone()` call. The code shape
  will not change. Only the flag word will.
- The difference between "share the parent's X" and "get a separate X" is the
  conceptual core of namespaces. For memory it is "share or copy". For
  namespaces it is "share or create new".
- Container runtimes written in Go cannot freely choose clone flags for the
  Go runtime's own threads. This is one reason runc uses a C bootstrap to
  create namespaces before the Go runtime starts additional threads
  (Chapter 11).

## Questions to think about

1. In `share` mode, what would happen if the child called `printf` while the
   parent was also printing? Why does `child_fn` avoid stdio?
2. Add `CLONE_FILES` only (not `CLONE_VM`). Predict all three results, then
   verify.
3. Why must the caller allocate a stack for the child in `clone()`, while
   `fork()` needs no stack argument? (Hint: what does `CLONE_VM` imply about
   the parent's stack?)
4. In a JVM thread dump, each Java thread has an `nid=0x...` value. Convert
   one from hex to decimal and look for it under `/proc/<jvm-pid>/task/`. What
   does that tell you about how Java threads map to Linux tasks?
