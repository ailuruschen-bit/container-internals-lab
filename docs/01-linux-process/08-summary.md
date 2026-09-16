# 8. Chapter Summary: What a Container Runtime Inherits

## The model you should now have

A Linux process is a kernel data structure (`task_struct`) plus an address
space. Everything it does outside its own memory goes through system calls,
and the kernel evaluates each call **using the calling task's properties**.

Processes are created by `clone()` (which `fork()` uses), and start running a
program with `execve()`. Between those two calls, the new process can change
its own properties with ordinary syscalls. `execve()` replaces the program but
preserves most of those properties.

```text
            parent process
                  │
                  │ clone(flags)      share or copy each resource
                  ▼                   (CLONE_NEW* flags: preview of Ch. 03)
            child process (same program)
                  │
                  │ configure itself:
                  │   dup2 / close / O_CLOEXEC        file descriptors   (§5)
                  │   chdir                           filesystem context (§3)
                  │   sigaction / sigprocmask         signal state       (§6)
                  │   setgroups / setresgid / setresuid  credentials     (§7)
                  │
                  │ execve(path, argv, envp)
                  ▼
            same PID, new program, configured properties preserved
```

## Properties table

This table collects the rules from all sections. Later chapters add rows
(namespaces, cgroups, capabilities, seccomp, root directory), but none of them
changes the pattern.

| Property | Stored in | After `fork()` | After `execve()` | Section |
|---|---|---|---|---|
| PID | `task_struct` | new | preserved | 2 |
| Parent (PPID) | `task_struct` | the caller | preserved | 2 |
| Memory | `mm` | copy-on-write copy | replaced | 3 |
| Threads | thread group | only the calling thread | only the calling thread | 1, 3 |
| `argv`, environment | new stack | copy | whatever is passed to `execve()` | 3 |
| File descriptor table | `files` | copy (same open file descriptions) | preserved, except close-on-exec | 5 |
| Working directory, root directory | `fs` | copy | preserved | 3 |
| Signal handlers | `sighand` | copy | reset to default | 6 |
| Ignored signals, signal mask | `sighand`, task | copy | preserved | 6 |
| UIDs, GIDs, groups | `cred` | copy | preserved, except set-user-ID files | 7 |
| Capabilities | `cred` | copy | recalculated (Ch. 05) | 7 |
| Namespaces | `nsproxy`, `cred` | same as parent, unless `CLONE_NEW*` | preserved | 3 (Ch. 03) |
| cgroup | `cgroups` | same as parent | preserved | (Ch. 04) |
| Seccomp filter, `no_new_privs` | task | copy | preserved | (Ch. 06) |

## The sentence to remember

> A container runtime is a program that creates a process, changes that
> process's kernel-visible properties using system calls, and then
> `execve()`s the application. The application runs in a container because
> those properties survive `execve()`.

## What has *not* been explained yet

Being honest about the gaps is part of the model:

- **How a process can see different PIDs, hostnames, mounts, or network
  interfaces** than its parent. We have only seen that `clone()` accepts
  `CLONE_NEW*` flags. That is Chapter 03, and it needs Chapter 02 first,
  because the mount namespace isolates a mount table we have not yet studied.
- **How resource usage is limited.** Nothing in this chapter can limit memory or
  CPU for a group of processes. `setrlimit()` limits are mostly per-process and much
  weaker. That is Chapter 04.
- **How UID 0 can be less than fully privileged.** We saw that capabilities
  exist and are cleared when leaving UID 0. The rules are Chapter 05.

## Self-check questions

Try answering these without looking back. Each one maps to a section.

1. Name three system calls that `cat /etc/hostname` makes, and explain why
   `open()` appears as `openat` in `strace`. (§1)
2. Why is a zombie process a problem even though it uses no memory? Who
   reaps an orphan? (§2)
3. What is the "gap" between `fork()` and `execve()`, and why is it the key
   idea behind container runtimes? (§3)
4. In `clone()`, what is the difference between `CLONE_FILES` and not setting
   it? And what does a `CLONE_NEW*` flag do differently from sharing flags? (§3)
5. How would you find, from the host, the environment variables and the open
   files of process 4321? (§4, §5)
6. Why can a leaked file descriptor defeat filesystem isolation? (§5)
7. A container's main process is `sh -c "java -jar app.jar"`. Explain, step by
   step, why `SIGTERM` might not trigger the JVM's shutdown hooks. (§2, §6)
8. Why does UID 1000 inside a container own files as UID 1000 on the host, even
   if the names differ? (§7)

## Next: Chapter 02 — Linux filesystem

Chapter 02 builds the second foundation: the Virtual File System, what a
mount is, the per-process mount table in `/proc/<pid>/mountinfo`, bind mounts,
mount propagation, and the difference between a process's root directory and
the filesystem root. These concepts are required before the mount namespace,
`pivot_root`, and OverlayFS can be explained without hand-waving.
