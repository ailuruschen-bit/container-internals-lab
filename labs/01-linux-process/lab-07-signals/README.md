# Lab 07 — Signals: Dispositions, Inheritance, and Forwarding

## Goal

Produce evidence that:

1. a handler changes what `SIGTERM` does, but nothing changes `SIGKILL`;
2. a process killed by signal N is reported with exit code 128 + N;
3. ignored signals stay ignored across `execve()`, while handled signals are
   reset to default;
4. a process does not forward signals to its children;
5. Ctrl-C targets a process group, not a single process.

## Prerequisites

- Linux VM with `bash` and `procps`. No root access needed.
- Read: [6. Signals](../../../docs/01-linux-process/06-signals.md).

## Background

Disposition masks are visible in `/proc/<pid>/status` as hexadecimal bit masks
(`SigIgn`, `SigCgt`). Bit *n−1* represents signal *n*. For `SIGTERM` (15), that
is bit 14, value `0x4000`. A small helper decodes a mask:

```bash
decode() { local m=$((16#$1)); for i in $(seq 1 64); do (( m & (1 << (i-1)) )) && printf '%s ' "$(kill -l $i)"; done; echo; }
```

Paste that function into your shell before starting.

## Experiment

### Part A — Default, handled, and SIGKILL

```bash
bash -c 'sleep 300' &
P=$!; sleep 0.2
kill -TERM $P; wait $P; echo "exit code: $?"
```

```bash
bash -c 'trap "echo got SIGTERM, cleaning up; exit 0" TERM; while :; do sleep 0.2; done' &
P=$!; sleep 0.5
grep SigCgt /proc/$P/status
kill -TERM $P; wait $P; echo "exit code: $?"
```

**Predict first.** A process traps `SIGTERM` *and* `SIGKILL`. What happens on
`kill -KILL`?

```bash
bash -c 'trap "echo never printed" TERM KILL; while :; do sleep 0.2; done' &
P=$!; sleep 0.5
kill -KILL $P; wait $P; echo "exit code: $?"
```

### Part B — What survives execve()

**Predict first.** In each case, bash sets a disposition for `SIGTERM` and
then `exec`s `sleep`. Will `sleep` die on `SIGTERM`?

Case 1: ignored before exec

```bash
bash -c 'trap "" TERM; exec sleep 300' &
P=$!; sleep 0.2
cat /proc/$P/comm; decode "$(awk '/^SigIgn/ {print $2}' /proc/$P/status)"
kill -TERM $P; sleep 0.2
ps -o pid,stat,comm -p $P || echo "process is gone"
kill -KILL $P
```

Case 2: handled before exec

```bash
bash -c 'trap "echo handler" TERM; exec sleep 300' &
P=$!; sleep 0.2
decode "$(awk '/^SigCgt/ {print $2}' /proc/$P/status)"
kill -TERM $P; wait $P; echo "exit code: $?"
```

### Part C — Signals are not forwarded to children

```bash
bash -c 'sleep 300; echo "this command keeps bash as the parent"' &
P=$!; sleep 0.2
ps -o pid,ppid,comm --ppid $P
CHILD=$(pgrep -P $P sleep)
kill -TERM $P; sleep 0.2
ps -o pid,ppid,stat,comm -p $CHILD
kill $CHILD
```

The trailing `echo` prevents bash from optimizing the single command into an
`exec`.

### Part D — Ctrl-C and process groups

```bash
ps -o pid,pgid,sid,tpgid,comm -p $$
sleep 300 | cat &
ps -o pid,pgid,sid,tpgid,comm --sid "$(ps -o sid= -p $$)"
kill -INT -- -$(ps -o pgid= -p $! | tr -d ' ')
sleep 0.2; jobs
```

`kill -- -PGID` sends the signal to every process in the group, like the
terminal does for Ctrl-C.

## Expected observations

**Part A.**
1. `exit code: 143` (128 + 15).
2. `SigCgt` is non-zero; the message `got SIGTERM, cleaning up` appears;
   `exit code: 0`.
3. `exit code: 137` (128 + 9). "never printed" is never printed. Bash may
   silently refuse to trap `KILL`.

**Part B.**
- Case 1: `comm` is `sleep`. `decode` prints `TERM` (possibly with other
  inherited ignored signals). After `kill -TERM`, `ps` still shows the process.
  Only `SIGKILL` stops it.
- Case 2: `decode` of `SigCgt` does **not** include `TERM`, and the process
  dies with `exit code: 143`. "handler" is never printed.

**Part C.** After `kill -TERM` to bash, bash is gone, but the `sleep` child is
still alive with state `S` and **PPID changed** to 1 or a subreaper
(compare Lab 02).

**Part D.** Both `sleep` and `cat` share one PGID, different from your
shell's PGID. They are in the same session (SID) as your shell. After the
group `SIGINT`, `jobs` reports the pipeline as terminated (`Interrupt`).

## Why this happens

- **A.** `trap` installs a handler with `sigaction()`. The kernel rejects any
  attempt to change `SIGKILL`'s disposition (`EINVAL`), and always terminates.
- **B.** `execve()` replaces the program, so a handler function address would
  point into memory that no longer exists; the kernel resets handled signals
  to `SIG_DFL`. `SIG_IGN` is not an address, so it is kept.
- **C.** `kill()` targets one process. Bash had no handler and died with the
  default action. Nothing told `sleep`, which became an orphan.
- **D.** A pipeline is placed in its own process group by the shell. Terminal
  signals and `kill(-pgid)` target the group.

## Connection to containers

- Part A reproduces container exit codes 143 and 137.
- Part B Case 1 is why a container runtime must reset signal dispositions
  before starting the application: an ignored `SIGTERM` inherited from a parent
  would make graceful shutdown impossible.
- Part C is the "shell as entrypoint" problem without the PID 1 part. Add the
  PID 1 rule from [section 2](../../../docs/01-linux-process/02-process-tree.md)
  and the shell would not even die. Chapter 03 reproduces that.
- Part D explains why `docker run -it` with a terminal behaves differently from
  a detached container: with a TTY, Ctrl-C reaches the foreground process group
  through the terminal driver.

## Questions to think about

1. Decode the `SigCgt` mask of a running JVM. Which signals does it handle, and
   why might the JVM need `SIGSEGV`?
2. In Part C, how would you write a shell entrypoint that *does* stop its child
   on `SIGTERM`? What is simpler than writing that trap?
3. The kernel OOM killer uses `SIGKILL`. What does that imply for JVM shutdown
   hooks and for flushing logs when a container exceeds its memory limit?
4. Why do you think `SIGKILL` and `SIGSTOP` were designed to be impossible to
   catch?
