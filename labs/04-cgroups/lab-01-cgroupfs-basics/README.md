# Lab 01 — cgroupfs Basics: Create, Join, Enable, Kill

## Goal

Produce evidence that:

1. a cgroup is a directory, and its interface is a set of files;
2. writing a PID to `cgroup.procs` moves a process, and children inherit the
   cgroup across `fork()` and `execve()`;
3. controllers must be enabled top-down through `cgroup.subtree_control`;
4. the "no internal processes" rule is enforced;
5. `cgroup.events`, `cgroup.freeze`, and `cgroup.kill` act on the whole group.

## Prerequisites

- Linux VM with cgroup v2 (`stat -fc %T /sys/fs/cgroup` prints `cgroup2fs`), `sudo`.
- Read: [2. The cgroup v2 hierarchy](../../../docs/04-cgroups/02-hierarchy-and-cgroupfs.md).

## Background

We create cgroups directly under the root, like the `cgroupfs` driver does.
systemd tolerates this for experiments. Use a root shell for convenience:

```bash
sudo -i
CG=/sys/fs/cgroup
```

## Experiment

### Part A — Where am I?

```bash
cat /proc/$$/cgroup
cat $CG/cgroup.controllers
cat $CG/cgroup.subtree_control
ls $CG | head -n 30
```

### Part B — Create a cgroup

**Predict first.** After `mkdir`, will `lab` contain `memory.max`?

```bash
mkdir $CG/lab
ls $CG/lab
cat $CG/lab/cgroup.controllers
```

If `memory`, `cpu`, `pids`, or `io` are missing from `lab/cgroup.controllers`,
enable them at the root:

```bash
echo "+cpu +memory +pids +io" > $CG/cgroup.subtree_control
cat $CG/lab/cgroup.controllers
ls $CG/lab | grep -E '^(cpu|memory|pids|io)\.' | head
```

### Part C — Move a process; children follow

```bash
sleep 1000 &
S=$!
cat /proc/$S/cgroup
echo $S > $CG/lab/cgroup.procs
cat /proc/$S/cgroup
cat $CG/lab/cgroup.procs
```

Now move a shell and start children from it:

```bash
bash -c 'echo $$ > /sys/fs/cgroup/lab/cgroup.procs; sleep 1000 & exec sh -c "cat /proc/self/cgroup; sleep 1000"' &
sleep 1
cat $CG/lab/cgroup.procs
for p in $(cat $CG/lab/cgroup.procs); do echo "$p $(cat /proc/$p/comm) $(cat /proc/$p/cgroup)"; done
```

### Part D — `cgroup.events` and `cgroup.freeze`

```bash
cat $CG/lab/cgroup.events
echo 1 > $CG/lab/cgroup.freeze
sleep 0.5
cat $CG/lab/cgroup.events
ps -o pid,stat,comm -p "$(paste -sd, $CG/lab/cgroup.procs)"
echo 0 > $CG/lab/cgroup.freeze
ps -o pid,stat,comm -p "$(paste -sd, $CG/lab/cgroup.procs)"
```

### Part E — Top-down and "no internal processes"

**Predict first.** `lab` contains processes. What happens when you enable the
memory controller for its children?

```bash
echo "+memory" > $CG/lab/cgroup.subtree_control; echo "exit code: $?"
```

Fix it by moving the processes into a leaf:

```bash
mkdir $CG/lab/leaf
for p in $(cat $CG/lab/cgroup.procs); do echo $p > $CG/lab/leaf/cgroup.procs; done
echo "+memory" > $CG/lab/cgroup.subtree_control; echo "exit code: $?"
cat $CG/lab/leaf/cgroup.controllers
echo $$ > $CG/lab/cgroup.procs; echo "move shell into lab, exit code: $?"
```

### Part F — Kill the group

```bash
cat $CG/lab/cgroup.events
echo 1 > $CG/lab/cgroup.kill
sleep 0.5
cat $CG/lab/leaf/cgroup.procs; echo "(empty?)"
cat $CG/lab/cgroup.events
```

### Cleanup

```bash
rmdir $CG/lab/leaf $CG/lab
exit    # leave the root shell
```

## Expected observations

**Part A.** Your shell is in something like
`0::/user.slice/user-1000.slice/session-3.scope`. The root `cgroup.controllers`
lists `cpuset cpu io memory hugetlb pids rdma misc` (varies by kernel).

**Part B.** Right after `mkdir`, `lab` contains core files (`cgroup.procs`,
`cgroup.events`, ...) and controller files only for controllers enabled in the
root's `cgroup.subtree_control`. After enabling, `memory.max`, `cpu.max`,
`pids.max`, and `io.max` appear.

**Part C.** The `sleep` moves from its session scope to `0::/lab`. In the second
experiment, `cgroup.procs` lists `sh` (the exec'd bash) and both `sleep`
processes; all show `0::/lab`, although only the bash wrote its own PID.

**Part D.** `populated 1`, `frozen 0`. After freezing, `frozen 1`, and the
processes show state `S` but no longer run (a frozen `sleep` does not finish
even after its timeout; `ps` may show state `S` or `D` depending on version).
After unfreezing, they continue.

**Part E.** The first write fails with `Device or resource busy`. After moving
all processes to `leaf`, enabling `+memory` succeeds, `leaf/cgroup.controllers`
includes `memory`, and moving the shell into `lab` itself fails with
`Device or resource busy`.

**Part F.** Before: `populated 1`. After `cgroup.kill`, `leaf/cgroup.procs` is
empty and `cgroup.events` shows `populated 0`. The background jobs in your root
shell are reported as `Killed`.

## Why this happens

- **B.** Controller interface files are created only for controllers enabled in
  the parent's `cgroup.subtree_control`.
- **C.** `cgroup_post_fork()` puts a new task into its parent's cgroup (a
  `css_set`); `execve()` does not touch it.
- **D, F.** The freezer and `cgroup.kill` walk every task in the subtree inside
  the kernel, so processes forked during the operation are also caught.
- **E.** The kernel refuses to enable domain controllers for children of a cgroup
  that has its own processes, and refuses to add processes to such a cgroup.

## Connection to containers

- `docker pause` uses the cgroup freezer; `docker kill` of a whole container can
  rely on `cgroup.kill`.
- Part C is why runtimes join the cgroup **before** executing the application:
  every process it later creates is contained automatically.
- Part E is why a Kubernetes pod cgroup contains only per-container child
  cgroups, never processes directly.

## Questions to think about

1. In Part C, would a process that calls `setsid()` or daemonizes (double fork)
   escape the cgroup? Why is that different from escaping a process tree?
2. Why can `cgroup.kill` be more reliable than `kill -9` on every PID listed in
   `cgroup.procs`?
3. What would you need to change so that a non-root user could create cgroups
   below `lab` and move their own processes into them?
4. Find the cgroup of a running `containerd` or `dockerd` on a host that has one.
   Which systemd unit type contains it?
