# Lab 02 — The Memory Controller: max, high, Page Cache, and the OOM Killer

## Goal

Produce evidence that:

1. exceeding `memory.max` with anonymous memory causes a cgroup-local OOM kill
   with `SIGKILL` (exit code 137);
2. the kill is recorded in `memory.events` and the kernel log;
3. `memory.high` throttles instead of killing;
4. page cache is charged to the cgroup but reclaimed instead of causing OOM;
5. tmpfs data is charged and cannot simply be reclaimed;
6. `memory.oom.group` kills every process in the cgroup together.

## Prerequisites

- Linux VM with cgroup v2, `sudo`, `python3`, at least 1 GiB of RAM.
- Read: [3. The memory controller](../../../docs/04-cgroups/03-memory-controller.md).

## Background

A small helper runs a command inside a cgroup: a shell that first writes its own
PID into `cgroup.procs`, then `exec`s the command (the create/configure/execute
pattern from Chapter 01).

```bash
sudo -i
CG=/sys/fs/cgroup
echo "+memory" > $CG/cgroup.subtree_control
mkdir -p $CG/labmem
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }
cd /path/to/this/lab     # where memhog.py is
```

Swap makes results less predictable. Disable swap usage for this cgroup:

```bash
echo 0 > $CG/labmem/memory.swap.max
```

## Experiment

### Part A — Hit `memory.max`

**Predict first.** A process allocates 300 MB in a cgroup with a 100 MB limit.
Will it get a Python `MemoryError`, or something else?

```bash
echo 100M > $CG/labmem/memory.max
cat $CG/labmem/memory.events
incg labmem python3 memhog.py 20 15 0.2; echo "exit code: $?"
cat $CG/labmem/memory.events
dmesg | grep -iE 'memory cgroup out of memory|oom-kill|Killed process' | tail -n 3
```

### Part B — `memory.high` throttles

```bash
echo 300M > $CG/labmem/memory.max
echo 100M > $CG/labmem/memory.high
grep high $CG/labmem/memory.events
incg labmem python3 memhog.py 20 8 0; echo "exit code: $?"
grep high $CG/labmem/memory.events
cat $CG/labmem/memory.pressure
echo max > $CG/labmem/memory.high
```

### Part C — Page cache is charged, but reclaimable

```bash
echo 100M > $CG/labmem/memory.max
sync; echo 3 > /proc/sys/vm/drop_caches          # start with a cold cache (lab VM only)
grep -E '^(anon|file) ' $CG/labmem/memory.stat
incg labmem dd if=/dev/zero of=/var/tmp/labmem.dat bs=1M count=400 status=none; echo "dd exit code: $?"
cat $CG/labmem/memory.current
grep -E '^(anon|file|file_dirty|file_writeback) ' $CG/labmem/memory.stat
grep -E '^(oom|oom_kill) ' $CG/labmem/memory.events
incg labmem cat /var/tmp/labmem.dat > /dev/null; echo "read exit code: $?"
cat $CG/labmem/memory.peak 2>/dev/null
rm /var/tmp/labmem.dat
```

**Predict first.** The cgroup writes and reads a 400 MB file with a 100 MB
limit. Will anything be OOM-killed?

### Part D — tmpfs is charged and not reclaimable

```bash
mkdir -p /tmp/labmem-tmpfs && mount -t tmpfs -o size=1G labmem /tmp/labmem-tmpfs
before=$(awk '/^oom_kill /{print $2}' $CG/labmem/memory.events)
incg labmem dd if=/dev/zero of=/tmp/labmem-tmpfs/fill bs=1M count=300 status=none; echo "dd exit code: $?"
ls -lh /tmp/labmem-tmpfs/fill
grep -E '^(shmem|file) ' $CG/labmem/memory.stat
echo "oom_kill before: $before, after: $(awk '/^oom_kill /{print $2}' $CG/labmem/memory.events)"
cat $CG/labmem/memory.current
```

Notice that the data stays charged after `dd` exits:

```bash
rm /tmp/labmem-tmpfs/fill
cat $CG/labmem/memory.current
umount /tmp/labmem-tmpfs
```

### Part E — `memory.oom.group`

Start two processes in the cgroup, one small and one growing:

```bash
echo 150M > $CG/labmem/memory.max
echo 0 > $CG/labmem/memory.oom.group
incg labmem sleep 1000 &
incg labmem python3 memhog.py 20 15 0.2; echo "memhog exit code: $?"
cat $CG/labmem/cgroup.procs; echo "(is sleep still alive?)"
```

Now with group kill:

```bash
echo 1 > $CG/labmem/memory.oom.group
incg labmem python3 memhog.py 20 15 0.2; echo "memhog exit code: $?"
sleep 0.5; cat $CG/labmem/cgroup.procs; echo "(is sleep still alive?)"
grep oom_group_kill $CG/labmem/memory.events
```

### Cleanup

```bash
echo 1 > $CG/labmem/cgroup.kill 2>/dev/null; sleep 0.5
rmdir $CG/labmem
exit
```

## Expected observations

**Part A.** `memhog` prints roughly `allocated 80 MB` or `100 MB`, then the
shell prints `Killed` and `exit code: 137`. No `MemoryError`. `memory.events`
now shows `oom 1` and `oom_kill 1` (or higher). `dmesg` shows lines such as:

```text
Memory cgroup out of memory: Killed process 5123 (python3) total-vm:..., anon-rss:98304kB, ...
```

**Part B.** All 160 MB are allocated and the exit code is `0`, but steps above
100 MB take visibly longer (for example 0.2–2 s each instead of ~0.02 s).
The `high` counter in `memory.events` increases, and `memory.pressure` shows
non-zero `some` averages.

**Part C.** Nothing is killed: `dd` and `cat` exit with `0`, `oom_kill` is
unchanged. `memory.current` is close to (but not above) 100 MB, and
`memory.stat` shows most of it as `file`, with `anon` small. `memory.peak`
is around 100 MB.

**Part D.** The file cannot grow to 300 MB. Depending on the kernel version,
`dd` is either **OOM-killed** (exit code 137, `oom_kill` increases) or fails with
`No space left on device`/`Cannot allocate memory`; in both cases `shmem` is
close to 100 MB. After `dd` exits, `memory.current` **stays** high until the
file is deleted.

**Part E.** Without `oom.group`: `memhog` is killed (137), but `sleep` is still
listed in `cgroup.procs`. With `oom.group=1`: after the OOM, both processes are
gone, and `oom_group_kill` is non-zero.

## Why this happens

- **A.** Anonymous pages cannot be reclaimed without swap (`memory.swap.max=0`),
  so the charge fails, the cgroup OOM killer picks the largest task in the
  cgroup, and sends `SIGKILL`. Python never sees an allocation failure: the
  memory was already "allocated" virtually; the failure happens when a page is
  touched.
- **B.** Above `memory.high`, the kernel applies reclaim and a penalty sleep to
  the allocating task (`mem_cgroup_handle_over_high()`), instead of failing.
- **C.** Clean and dirty page cache pages are reclaimed (after writeback) when
  the limit is reached.
- **D.** tmpfs pages are `shmem`: without swap they are not reclaimable, and they
  belong to the filesystem, not to the process, so they outlive the writer.
- **E.** With `memory.oom.group`, the OOM killer kills all tasks of the cgroup
  instead of one.

## Connection to containers

- Part A is `OOMKilled: true`, exit code 137. Look at `memory.events` of the
  container's cgroup to confirm it on a real host.
- Part C explains why a container's reported memory usage can sit near its limit
  while the application is healthy.
- Part D is the classic "writing to an in-memory `emptyDir` or `/dev/shm` got my
  container OOM-killed", and why the memory is still in use after the writer
  exits.
- Part E is Kubernetes' behavior on cgroup v2: the whole container is killed.

## Questions to think about

1. A JVM runs with `-Xmx` equal to the container limit. Explain step by step why
   it is killed with exit code 137 instead of throwing `OutOfMemoryError`.
2. Why might `memory.high` be a better tool than `memory.max` for a batch job
   that you prefer to run slowly rather than fail?
3. In Part C, `dd` was in the cgroup. Who pays for the page cache if a process
   **outside** the cgroup reads the file first?
4. How would you design an alert for "this container is about to be OOM-killed"
   that is not fooled by page cache? Which files would you read?
