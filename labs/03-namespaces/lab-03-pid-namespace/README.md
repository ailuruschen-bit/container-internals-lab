# Lab 03 — PID Namespace: Two Numberings, PID 1, and /proc

## Goal

Produce evidence that:

1. a process in a new PID namespace has two PIDs, visible in `NSpid`;
2. `unshare --pid` affects only children, and a namespace dies with its init;
3. `ps` needs a new procfs to show the namespace's view;
4. orphans are reparented to the namespace's PID 1;
5. PID 1 of a namespace ignores signals without handlers, except `SIGKILL` and
   `SIGSTOP` from the host;
6. killing a namespace's PID 1 kills every process in it.

This lab reproduces, without any container software, the "shell as PID 1 does
not stop on SIGTERM" problem from Chapter 01.

## Prerequisites

- Linux VM with `sudo`, `gcc`, `util-linux`, `procps`.
- Read: [3. PID namespace](../../../docs/03-namespaces/03-pid-namespace.md).

## Background

`unshare --pid --fork` runs `unshare(CLONE_NEWPID)`, then `fork()`s; the child
is PID 1 in the new namespace and `execve()`s the command. The `unshare`
process itself stays in the host PID namespace as the child's parent.

To find the host PID of a namespace's PID 1, use:

```bash
nsinit() { ps -o pid= --ppid "$(pgrep -n -x unshare)" | tr -d ' '; }
```

## Experiment

### Part A — Without `--fork`

**Predict first.** The command below creates a new PID namespace, runs `bash`
(not in the new namespace), and in that bash runs `echo`, `ls`, and `ls`
again. Which commands succeed?

```bash
sudo unshare --pid bash -c 'echo "bash pid: $$"; ls /proc/self/ns/pid >/dev/null && echo "first child ok"; ls >/dev/null && echo "second child ok"'
```

### Part B — With `--fork`, before and after remounting /proc

```bash
sudo unshare --pid --fork bash -c 'echo "my pid: $$"; ps -e | wc -l'
sudo unshare --pid --fork --mount-proc bash -c 'echo "my pid: $$"; ps -ef; grep NSpid /proc/self/status'
```

### Part C — The same, in C

```bash
gcc -Wall -o pid_clone pid_clone.c
sudo ./pid_clone
```

### Part D — Two numberings of one process

Terminal 2:

```bash
sudo unshare --pid --fork --mount-proc bash
sleep 1000 &
ps -o pid,ppid,comm
```

Terminal 1:

```bash
nsinit() { ps -o pid= --ppid "$(pgrep -n -x unshare)" | tr -d ' '; }
INIT=$(nsinit); echo "host PID of the namespace's PID 1: $INIT"
grep NSpid /proc/$INIT/status
grep NSpid /proc/$(pgrep -n -x sleep)/status
ps -o pid,ppid,comm --ppid $INIT
```

In terminal 2, exit the shell (`exit`) and check from terminal 1 that the
`sleep` is gone: `pgrep -x sleep || echo "sleep was killed"`.

### Part E — Orphans go to the namespace's PID 1

```bash
sudo unshare --pid --fork --mount-proc bash -c '
  sh -c "sleep 30 & echo orphan-to-be: \$!"
  sleep 0.5
  ps -o pid,ppid,comm'
```

### Part F — The PID 1 signal rule

Terminal 2 (PID 1 is `sleep`, which installs no signal handlers):

```bash
sudo unshare --pid --fork --mount-proc sleep 1000
```

Terminal 1:

**Predict first.** Which of these signals will stop the process?

```bash
INIT=$(nsinit)
sudo kill -TERM $INIT; sleep 0.5; ps -o pid,stat,comm -p $INIT
sudo kill -INT  $INIT; sleep 0.5; ps -o pid,stat,comm -p $INIT
sudo kill -KILL $INIT; sleep 0.5; ps -o pid,stat,comm -p $INIT || echo "gone"
```

Compare with a PID 1 that **does** handle `SIGTERM`:

```bash
sudo unshare --pid --fork --mount-proc bash -c 'trap "echo got TERM; exit 0" TERM; while :; do sleep 0.2; done' &
sleep 1; INIT=$(nsinit)
sudo kill -TERM $INIT; wait
```

### Part G — The "shell as entrypoint" problem, reproduced

```bash
sudo unshare --pid --fork --mount-proc sh -c 'sleep 1000; echo done' &
sleep 1; INIT=$(nsinit)
ps -o pid,ppid,comm --ppid $INIT              # the "application": sleep
sudo kill -TERM $INIT; sleep 2
ps -o pid,comm -p $INIT && echo "still running after SIGTERM"
sudo kill -KILL $INIT; sleep 0.5
pgrep -x sleep || echo "the whole namespace is gone"
```

## Expected observations

**Part A.** `bash pid:` prints a normal host PID. `first child ok` is printed.
The next command fails with `bash: fork: Cannot allocate memory`. The first
child was PID 1 of the new namespace; when it exited, the namespace became
unusable.

**Part B.** First command: `my pid: 1`, but `ps -e | wc -l` counts all host
processes. Second command: `my pid: 1`; `ps -ef` shows only `bash` (PID 1) and
`ps`; `NSpid` shows only `1`, because the new procfs belongs to the new
namespace and shows only its own level.

**Part C.**

```text
parent getpid()=6000
parent sees the child as pid=6001
child  getpid()=1 getppid()=0
child  NSpid:  6001    1
child  NSpid:  1
child  processes visible through the new /proc:
    PID    PPID COMMAND
      1       0 ps
```

The first `NSpid` line is read through the host's procfs (two levels), the
second through the new one.

**Part D.** Inside: bash is PID 1, `sleep` is PID 2 with PPID 1. From the host:
`NSpid: <host pid> 1` for bash and `NSpid: <host pid> 2` for sleep. After
`exit`, `sleep` is gone.

**Part E.** The `sleep 30` process shows `PPID 1`: its parent `sh` exited, and
it was reparented to the namespace's init (bash), not to the host's PID 1.

**Part F.** `SIGTERM` and `SIGINT` have **no effect**: the process is still in
state `S`. `SIGKILL` removes it. With the `trap`, `got TERM` is printed and the
namespace ends.

**Part G.** After `SIGTERM`, `still running after SIGTERM` is printed: `sh` is
PID 1, has no handler, and the signal is dropped; the `sleep` "application"
never received anything. After `SIGKILL` to PID 1, the `sleep` is gone too.

## Why this happens

- **A.** `unshare(CLONE_NEWPID)` only set `pid_ns_for_children`. When PID 1 of a
  namespace exits, `zap_pid_ns_processes()` runs and the namespace refuses new
  processes (`alloc_pid()` fails with `ENOMEM`).
- **B, C.** `getpid()` translates the task's `struct pid` into the caller's
  namespace. procfs shows PIDs as seen from the namespace it was mounted for.
  `getppid()` is 0 because the parent has no number in the child's namespace.
- **E.** `find_new_reaper()` (Chapter 01 Lab 02) stops at the init process of
  the orphan's PID namespace.
- **F, G.** Signals to a namespace init without a handler are ignored, except
  `SIGKILL`/`SIGSTOP` sent from an ancestor namespace. Init death triggers
  `SIGKILL` for all remaining members.

## Connection to containers

- Part B is why every runtime mounts a new `/proc` inside the container.
- Part F and Part G are precisely what happens on `docker stop` / pod
  termination when the entrypoint is a shell or a program without a `SIGTERM`
  handler: the grace period expires and the container is killed with exit code
  137.
- Part E is why a container's PID 1 must reap zombies, and why `tini` or
  `docker run --init` exist.

## Questions to think about

1. In Part D, can the bash inside the namespace send a signal to the host's
   `sshd`? Why not, even as root?
2. Why does the kernel kill all processes in the namespace when PID 1 exits,
   instead of reparenting them to the host's init?
3. How would you make Part G stop gracefully on `SIGTERM` without changing the
   `sleep` "application"? Give two different solutions.
4. `docker top` shows host PIDs, while `ps` inside the container shows small
   PIDs. Using `NSpid`, explain how a tool could translate between them.
