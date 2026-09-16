# Lab 03 — fork, execve, and the Gap Between Them

## Goal

Produce evidence that:

1. `fork()` returns twice and gives the child a *copy* of memory;
2. `execve()` keeps the PID but replaces the program;
3. configuration done between `fork()` and `execve()` (file descriptors,
   working directory) survives into the new program;
4. the environment is exactly what the caller passes to `execve()`;
5. a shell uses `clone` + `execve` for every external command.

## Prerequisites

- Linux VM with `gcc` and `strace`.
- Read: [3. fork, execve, and clone](../../../docs/01-linux-process/03-fork-exec-clone.md).

No root access is needed.

## Background

The program `fork_exec.c` reproduces what a shell does for
`cmd > file` and what a container runtime does at a larger scale: create a
process, configure it with ordinary syscalls, then `execve()` the target
program.

## Experiment

### Part A — Create, configure, execute

Read `fork_exec.c` first. Then **predict**, in writing:

1. Which PID will the `sh` program report with `$$`: the parent's, the
   child's, or a new one?
2. What will the working directory of `sh` be?
3. Will `HOME` be set inside `sh`?
4. After the child sets `counter = 100`, what value will the parent print?
5. Will the `[exec'd]` lines appear on your terminal?

```bash
gcc -Wall -o fork_exec fork_exec.c
./fork_exec
```

### Part B — Watch it at the syscall level

```bash
strace -f -e trace=clone,clone3,execve,dup2,chdir,wait4,exit_group ./fork_exec 2>&1 | grep -v '^\[parent\]\|^\[child\]'
```

`-f` follows child processes. Each line from a child is prefixed with
`[pid N]`.

### Part C — Your shell does the same thing

```bash
strace -f -e trace=clone,clone3,execve,openat,dup2 bash -c 'ls / > /tmp/ls_out.txt; true'
```

**Predict first.** Which process calls `openat` on `/tmp/ls_out.txt`: bash, or
`ls`?

### Part D — `exec` without `fork`

```bash
bash -c 'echo "before exec: $$"; exec sh -c "echo after exec: \$\$"'
```

## Expected observations

**Part A.**

```text
[parent] pid=5000 counter=0
[child]  pid=5001 ppid=5000 counter=100 (about to configure and exec)
[parent] child 5001 exited with status 7
[parent] counter in parent is still 0
[parent] contents of /tmp/fork_exec_output.txt:
[exec'd] pid=5001 cwd=/tmp
[exec'd] GREETING=hello-from-the-gap HOME=<unset>
```

Answers to the predictions: (1) the **child's** PID, 5001 in this example;
(2) `/tmp`; (3) no, `HOME=<unset>`; (4) `0`; (5) not directly; they went into
the file, and the parent printed the file afterwards.

**Part B.** Look for this sequence (addresses and flags vary):

```text
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, ...) = 5001
[pid 5001] dup2(3, 1)                    = 1
[pid 5001] chdir("/tmp")                 = 0
[pid 5001] execve("/bin/sh", ["sh", "-c", "echo ..."], ["GREETING=hello-from-the-gap"]) = 0
...
[pid 5001] exit_group(7)                 = ?
wait4(5001, [{WIFEXITED(s) && WEXITSTATUS(s) == 7}], 0, NULL) = 5001
execve("/usr/bin/cat", ["cat", "/tmp/fork_exec_output.txt"], ...) = 0
```

Note that the C code called `fork()`, but the trace shows `clone` with
`SIGCHLD` and no sharing flags. You may also see an extra `clone` made by `sh`
itself: the `$(pwd)` command substitution runs in a forked subshell.

**Part C.** The `openat("/tmp/ls_out.txt", O_WRONLY|O_CREAT|O_TRUNC, ...)` line
is prefixed with the **child's** `[pid N]`, and appears **before**
`execve("/usr/bin/ls", ...)` for that same PID. `ls` never opens the file.

**Part D.** Both lines print the **same** number.

## Why this happens

- `fork()` creates a new task with a copy-on-write copy of memory. Writing
  `counter` in the child triggers a private page copy; the parent's page is
  untouched.
- `dup2(fd, 1)` changes the child's file descriptor table, and `chdir()`
  changes its `fs` context. `execve()` preserves both.
- `execve()` replaces the address space but keeps the task, so `$$` in `sh`
  equals the child's PID.
- The environment is the `envp` array, and nothing else. `HOME` was not in it.
- A shell's `exec` builtin calls `execve()` *without* forking first, so the
  shell's own process becomes the new program.

## Connection to containers

- A container runtime does exactly Part A, with a longer list of
  configuration steps in the gap: namespaces, mounts, `pivot_root`, cgroups,
  capabilities, seccomp.
- The application's environment variables come from the runtime's `envp`, not
  from the runtime's own environment. This is why a container does not see the
  environment of the daemon that started it.
- Part D explains why container entrypoint scripts usually end with
  `exec "$@"`: the script's process (often PID 1 in the container) *becomes*
  the application. Without `exec`, the shell remains PID 1, and the
  application is its child, which changes signal delivery and reaping
  ([section 2](../../../docs/01-linux-process/02-process-tree.md)).

## Questions to think about

1. Remove the `fflush(stdout)` before `fork()` and redirect the program's
   output to a file: `./fork_exec > run.txt; cat run.txt`. Why does the first
   line appear twice? (Hint: where does unflushed stdio data live?)
2. Change `/bin/sh` in `execve` to `/bin/does-not-exist`. What does the parent
   report as the exit status, and why?
3. Why is it safe for the child to call `chdir()` without affecting the
   parent, while two threads calling `chdir()` would affect each other?
4. A JVM `ProcessBuilder.directory(dir)` sets the working directory of the new
   process. Based on this lab, at which point in the create/configure/execute
   sequence must that `chdir` happen?
