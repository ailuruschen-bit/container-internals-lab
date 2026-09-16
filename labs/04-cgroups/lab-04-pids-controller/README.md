# Lab 04 — The PIDs Controller: Fork Bombs, Threads, and RLIMIT_NPROC

## Goal

Produce evidence that:

1. `pids.max` makes `fork()` fail with `EAGAIN` instead of killing anything;
2. threads count against `pids.max`;
3. `RLIMIT_NPROC` counts processes per user across the machine, not per group;
4. a fork bomb inside a PID-limited cgroup does not affect the rest of the
   system, and `cgroup.kill` cleans it up.

## Prerequisites

- Linux VM with cgroup v2, `sudo`, `python3`.
- Read: [5. The PIDs and I/O controllers](../../../docs/04-cgroups/05-pids-and-io-controllers.md), Part 1.

## Setup

```bash
sudo -i
CG=/sys/fs/cgroup
echo "+pids" > $CG/cgroup.subtree_control
mkdir -p $CG/labpids
echo 20 > $CG/labpids/pids.max
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }
```

## Experiment

### Part A — Processes

**Predict first.** A shell in a cgroup with `pids.max=20` starts 50 background
`sleep` processes. How many start? Is anything killed?

```bash
incg labpids bash -c '
  for i in $(seq 1 50); do sleep 30 & done 2>/tmp/labpids-errors.txt
  echo "pids.current: $(cat /sys/fs/cgroup/labpids/pids.current)"
  echo "running sleeps: $(jobs -r | wc -l)"
  wait'
sort /tmp/labpids-errors.txt | uniq -c | head
cat $CG/labpids/pids.events
```

### Part B — Threads count too

```bash
incg labpids python3 -c '
import threading, time
started = 0
try:
    for i in range(50):
        threading.Thread(target=time.sleep, args=(5,), daemon=True).start()
        started += 1
except RuntimeError as e:
    print("RuntimeError:", e)
print("threads started:", started)
print("pids.current:", open("/sys/fs/cgroup/labpids/pids.current").read().strip())
'
```

### Part C — `RLIMIT_NPROC` is per user, not per group

Exit the root shell for this part and run as your normal user:

```bash
exit
ps -u "$(id -u)" --no-headers | wc -l          # how many processes your UID already has
bash -c 'ulimit -u 5; sleep 1 & echo "started: $!"'; echo "exit code: $?"
```

**Predict first.** The inner shell has started no processes yet. Can it start
one `sleep` with a limit of 5?

Then return: `sudo -i`, and re-run the setup lines (`CG=...`, `incg() {...}`).

### Part D — A contained fork bomb

This is a real fork bomb. It is safe **only** because the cgroup limits it. Do
it in a disposable VM. Open a second terminal first and keep `top` running there
to see that the system stays responsive.

```bash
echo 50 > $CG/labpids/pids.max
incg labpids bash -c 'bomb() { bomb | bomb & }; bomb' &
sleep 3
cat $CG/labpids/pids.current $CG/labpids/pids.events
ls / >/dev/null && echo "the rest of the system can still create processes"
echo 1 > $CG/labpids/cgroup.kill
sleep 1; cat $CG/labpids/pids.current
```

### Cleanup

```bash
rmdir $CG/labpids; rm -f /tmp/labpids-errors.txt
exit
```

## Expected observations

**Part A.** `pids.current` is `20` and roughly 17–18 sleeps are running (the
shell itself, the `cat`/`jobs` subprocesses, and command substitutions also use
PIDs). The error file contains many lines like
`bash: fork: retry: Resource temporarily unavailable` and
`bash: fork: Resource temporarily unavailable`. No process is killed.
`pids.events` shows `max` with a non-zero count.

**Part B.** Python raises `RuntimeError: can't start new thread` after about 18
threads, and `pids.current` is `20`.

**Part C.** If your user already has more than 5 processes (typical: a login
session, `systemd --user`, `sshd`), `sleep` **cannot** start:
`bash: fork: retry: Resource temporarily unavailable`, even though this shell
created nothing yet. The limit compares against the user's process count on the
whole machine.

**Part D.** `pids.current` stays at `50`; `pids.events` shows a growing `max`
count. The second terminal and new commands keep working. After `cgroup.kill`,
`pids.current` returns to `0`.

## Why this happens

- **A, B, D.** `copy_process()` calls `cgroup_can_fork()`, where the PIDs
  controller charges one task to every ancestor cgroup and refuses with `EAGAIN`
  if any would exceed its `pids.max`. Threads are tasks created by the same path.
- **C.** `RLIMIT_NPROC` is checked in `copy_process()` against the number of
  tasks owned by the real UID (`ucounts`), a per-user counter.
- **D.** `cgroup.kill` sends `SIGKILL` to all tasks in the cgroup inside the
  kernel, so the bomb cannot out-fork the cleanup.

## Connection to containers

- Part D is exactly why `--pids-limit` and Kubernetes `podPidsLimit` exist.
- Part B is a real JVM failure mode: `unable to create native thread` in a
  container can be caused by `pids.max`, not by memory.
- Part C explains why `ulimit -u` is not a container isolation mechanism,
  especially when many containers run as the same UID.

## Questions to think about

1. Why is failing `fork()` with `EAGAIN` a better policy for a PID limit than
   killing a process, as the memory controller does?
2. A Java application server in a container with `pids.max=256` handles
   traffic spikes by creating threads. What happens during a spike, and which
   Java exception would you look for?
3. Two containers both run as UID 1000 on the same host. Why would
   `RLIMIT_NPROC` couple their behavior, while `pids.max` does not?
