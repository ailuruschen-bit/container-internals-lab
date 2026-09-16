# Lab 02 — The Process Tree, Zombies, and Reaping

## Goal

Produce observable evidence that:

1. every process has a parent, and the tree leads back to PID 1;
2. an exited child stays a zombie until its parent reaps it;
3. orphans are reparented to PID 1 or to the nearest child subreaper;
4. a subreaper receives the exit status of processes it never created.

## Prerequisites

- Linux VM. Packages: `procps` (`ps`), `psmisc` (`pstree`), `gcc`.
- Read: [2. The process tree](../../../docs/01-linux-process/02-process-tree.md).

No root access is needed.

## Background

The kernel records each process's parent. When a process exits, its parent
receives `SIGCHLD` and is expected to call `wait4()`/`waitid()`. When a parent
exits first, its children are reparented to the nearest subreaper ancestor,
or to PID 1.

## Experiment

### Part A — Walk the tree to PID 1

```bash
echo "my shell: $$"
ps -o pid,ppid,user,comm -p $$
pstree -ps $$
```

`$$` is the shell's own PID. `pstree -s` shows the ancestors of a process, and
`-p` shows PIDs.

Then look at the top of the tree:

```bash
ps -o pid,ppid,comm -p 1,2
```

### Part B — Create a zombie on purpose

**Predict first.** In the command below, `sh` starts `sleep 1` in the
background, then *replaces itself* with `sleep 60` using `exec`. The new
`sleep 60` program has no idea it has a child. What will the `sleep 1` process
look like after 2 seconds?

```bash
sh -c 'sleep 1 & exec sleep 60' &
PARENT=$!
sleep 2
ps -o pid,ppid,stat,comm --ppid $PARENT
```

Clean up:

```bash
kill $PARENT
sleep 0.5
ps -o pid,ppid,stat,comm --ppid $PARENT   # prints only the header now
```

### Part C — Create an orphan

**Predict first.** `sh` starts `sleep 300` in the background and exits
immediately. What will the PPID of `sleep 300` be?

```bash
sh -c 'sleep 300 & echo "orphan pid: $!"'
sleep 0.5
ps -o pid,ppid,comm -C sleep
```

Find what the new parent is:

```bash
ps -o pid,comm -p "$(ps -o ppid= -C sleep | head -n1 | tr -d ' ')"
```

Clean up with `pkill -x -f 'sleep 300'`.

### Part D — A subreaper collects orphans

```bash
gcc -Wall -o subreaper subreaper.c
./subreaper
./subreaper --no
```

## Expected observations

**Part A.** `pstree` shows a chain such as
`systemd(1)───sshd(812)───sshd(2190)───bash(2201)───pstree(2350)`. PID 1 is
`systemd` (or `init`) with PPID 0. PID 2 is `kthreadd` with PPID 0.

**Part B.** One line with state `Z` and command `sleep`, for example:

```text
    PID    PPID STAT COMMAND
   2412    2411 Z    sleep
```

(`ps` may also display the name as `sleep <defunct>` in other formats.) After
killing the parent, the zombie disappears.

**Part C.** The PPID of `sleep 300` is **not** the `sh` PID printed earlier.
It is either `1` or the PID of a subreaper. On many systemd-based machines,
especially desktop and some SSH sessions, it is a `systemd --user` process,
which marks itself as a subreaper. In a minimal VM without a user session
manager, it is `1`.

**Part D.** With the flag:

```text
[subreaper]  pid=3000  subreaper=yes
[middle]     pid=3001  exiting now
[grandchild] pid=3002  ppid=3001 (parent is middle)
[subreaper]  reaped pid=3001  exit status=0
[grandchild] pid=3002  ppid=3000 (after middle exited)
[subreaper]  reaped pid=3002  exit status=42
[subreaper]  no children left
```

Line order around the first two lines may vary. The key evidence: the
grandchild's PPID changes to the subreaper's PID, and the subreaper reaps exit
status `42` from a process it never created.

With `--no`, the grandchild's PPID changes to `1` (or to a `systemd --user`
subreaper), and the program reports only `middle` as reaped. Its final
`printf` may appear after your shell prompt returns, because the grandchild is
no longer attached to `./subreaper`.

## Why this happens

- **Part B.** The kernel keeps the exit status of `sleep 1` until its parent
  calls `wait`. After `exec`, the parent's PID is running the `sleep` program,
  which never calls `wait`. Killing the parent orphans the zombie. It is
  reparented and immediately reaped by the new parent.
- **Part C / D.** On exit, the kernel's `forget_original_parent()` (in
  `kernel/exit.c`) calls `find_new_reaper()`, which walks up the ancestors to
  find a process with the subreaper flag, falling back to the init process.

## Connection to containers

- A container shim uses `PR_SET_CHILD_SUBREAPER` so that it, and not the
  host's PID 1, receives the exit status of the container's main process. That
  is how `docker ps -a` knows an exited container's exit code.
- Part B is the zombie problem in miniature. Replace `exec sleep 60` with
  `exec java -jar app.jar`, and replace `sleep 1 &` with a helper script the
  application launched. If that application is PID 1 in a container and does
  not reap orphans, zombies accumulate.
- A minimal container init such as `tini` is essentially the reaping loop at
  the bottom of `subreaper.c`, plus signal forwarding.

## Questions to think about

1. In Part B, why did the zombie disappear after its parent was killed, even
   though nobody explicitly called `wait` for it?
2. A zombie uses no memory. Why can accumulating zombies still break a system?
   Which resource runs out first?
3. In `subreaper.c`, what would happen if the program exited without the
   `wait` loop while the grandchild was still sleeping?
4. The shim is the subreaper for a container. Who reaps the shim when it exits?
