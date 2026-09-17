# Lab 03 — Tasks, the Shim, and Supervision

## Goal

Produce evidence that:

1. the container process's parent is `containerd-shim-runc-v2`, not `containerd`;
2. runc does not linger: no runc process remains once the container is running;
3. the container **survives a containerd restart** because the shim supervises it;
4. the shim reaps the container and holds its exit code (Chapter 01 §2).

## Prerequisites

- Linux VM with `containerd`, `ctr`, `sudo`, `procps`. A **disposable** VM: Part C
  restarts containerd.
- Read: [4. Containers, tasks, and the shim](../../../docs/12-containerd-internals/04-tasks-and-shim.md)
  and Chapter 01 §2.

## Experiment

### Part A — The process tree

```bash
sudo ctr run -d docker.io/library/alpine:latest demo sleep 600
PID=$(sudo ctr task ls | awk '/demo/{print $2}')
echo "container pid: $PID"
ps -o pid,ppid,comm -p "$PID"
PPID=$(ps -o ppid= -p "$PID" | tr -d ' ')
ps -o pid,ppid,comm -p "$PPID"
ps -ef --forest | grep -A2 -E 'containerd|shim|sleep' | grep -v grep | head
```

**Predict first.** Will the parent of the `sleep` be `containerd` or a shim?

### Part B — runc does not linger

```bash
pgrep -a runc || echo "no runc process is running"
pgrep -a containerd-shim || pgrep -a shim
```

### Part C — Survive a containerd restart

```bash
sudo systemctl restart containerd
sleep 2
ps -o pid,comm -p "$PID" && echo "the container is STILL running after containerd restarted"
sudo ctr task ls | grep demo
```

**Predict first.** Will the `sleep` still be alive after containerd restarts?

### Part D — The shim reaps and reports exit

```bash
sudo ctr task kill -s SIGKILL demo
sleep 1
sudo ctr task ls | grep demo || echo "task gone"
# The exit status was collected by the shim and reported to containerd:
sudo ctr events &                        # optional: watch events in another run
sudo ctr container rm demo 2>/dev/null
pgrep -a containerd-shim | grep demo || echo "the shim for demo exited after delete"
```

## Expected observations

**Part A.** The `sleep`'s parent is a `containerd-shim-runc-v2` process, **not**
`containerd`. `ps --forest` shows `containerd` and the shim as siblings under the
init/system manager, with the container process under the shim.

**Part B.** No `runc` process is running: runc did its setup and exited (Runtime
v2). One `containerd-shim-runc-v2` per container remains.

**Part C.** After `systemctl restart containerd`, the `sleep` is **still alive**
(same PID), and `ctr task ls` still shows `demo`: the shim kept it running and
containerd re-attached. This is the core reason the shim exists.

**Part D.** After `task kill`, the task is gone; the shim collected the exit
status (SIGKILL → 137, Chapter 01 §6) and reported it. After `container rm`, the
shim for `demo` exits.

## Why this happens

- The Tasks service starts a per-container shim; the shim (a subreaper,
  Chapter 01 §2) is the container's parent, owns its stdio, and outlives both runc
  and containerd restarts (§4).
- runc exits after `create`/`start` in Runtime v2, leaving only the shim (§4).

## Connection to containers

- This is why `docker restart` of the daemon does not kill your containers, and
  why `docker logs` works after a daemon restart: the shim, not the daemon, owns
  the process and its output.
- It is Chapter 01 §2 (subreapers, reaping, exit status) realized at the top of
  the stack — the payoff of the very first chapter.

## Questions to think about

1. Using Chapter 01 §2, explain exactly what would go wrong if containerd ran the
   container process as its own direct child instead of via a shim.
2. If the container double-forks a daemon, who reaps the orphan, and thanks to
   which `prctl` option? (Chapter 01 §2)
3. Where does `docker logs`/`ctr task attach` get its data from, given the shim
   owns stdio?
