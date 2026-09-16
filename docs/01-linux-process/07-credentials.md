# 7. Credentials: UID, GID, and Privilege

## The problem: who is asking?

Every system call is a request, and the kernel must decide whether to grant it.
May this process open `/etc/shadow`? Send `SIGTERM` to PID 812? Bind to port
80? To decide, the kernel needs an identity attached to every process. That
identity is the process's **credentials**.

## The kernel only knows numbers

The most important fact in this section is simple: **the kernel does not know
user names**. It only knows numeric **user IDs (UIDs)** and **group IDs
(GIDs)**.

The mapping from `alice` to `1000` lives in the file `/etc/passwd`, and from
`docker` to `998` in `/etc/group`. Only user-space programs (`ls`, `id`, `ps`,
libc's `getpwuid()`) read these files, to display names. The kernel never
does.

```console
$ ls -ln /etc/hostname
-rw-r--r-- 1 0 0 4 Jan 10 09:00 /etc/hostname
         owner UID ┘ └ owner GID
```

Keep this in mind. A container image carries its own `/etc/passwd`. Inside the
container, UID 1000 might be called `app`; on the host it might be called
`alice`. **Unless a user namespace remaps IDs (Chapter 05), it is the same
number, and therefore the same identity to the kernel.** Files created by the
container's `app` user on a shared volume are owned by `alice` on the host.

## The credential set of a process

In the kernel, credentials live in `struct cred`, referenced from
`task_struct`. The user-visible parts:

| Credential | Meaning |
|---|---|
| **Real UID / GID** | Who started the process. Used, for example, in `kill()` permission checks. |
| **Effective UID / GID** | Who the process is acting as. Used for most permission checks. |
| **Saved set-user-ID / set-group-ID** | A stored copy that lets a program temporarily drop and later regain its effective ID. |
| **Filesystem UID / GID** | Used for file permission checks. Linux-specific; almost always equal to the effective ID. |
| **Supplementary groups** | Additional group IDs, used in file permission checks. |
| **Capabilities** | Fine-grained privileges (Chapter 05). |

You saw these as the four columns of `Uid:` and `Gid:` in
`/proc/<pid>/status` (real, effective, saved, filesystem), and
`Groups:` for supplementary groups.

For a normal process, all four UIDs are equal. They differ only when a program
deliberately changes them, or when it was started from a **set-user-ID**
executable.

### How file permission checks use credentials

When a process opens a file, the kernel compares the process's filesystem UID
and GIDs with the file's owner, group, and mode bits:

1. If the UID is 0, traditionally skip most checks (see "root" below).
2. If the UID equals the file owner, use the owner bits (`rwx------`).
3. Else if the GID or any supplementary group equals the file's group, use the
   group bits (`---rwx---`).
4. Otherwise, use the "other" bits (`------rwx`).

## Where credentials come from

Credentials follow the same inheritance rules as other process properties:

- `fork()`: the child gets a copy of the parent's credentials.
- `execve()`: credentials are preserved, **except** for set-user-ID and
  set-group-ID executables (below), and except for capabilities, which are
  recalculated (Chapter 05).
- A process can change its own credentials with system calls, subject to
  permission rules.

The first process on the system, PID 1, runs as UID 0. When you log in, a
privileged program (`sshd`, `login`) authenticates you, then **changes its own
credentials** to your UID, GID, and groups, and finally `execve()`s your shell.
Every process you start inherits those credentials. This is the same
create/configure/execute pattern from [section 3](03-fork-exec-clone.md).

### Changing credentials

```c
#include <unistd.h>
#include <grp.h>
int setgroups(size_t size, const gid_t *list);        // supplementary groups
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);     // real, effective, saved GID
int setresuid(uid_t ruid, uid_t euid, uid_t suid);     // real, effective, saved UID
```

(`setuid()` and `setgid()` are older, simpler forms with subtle rules.)

A privileged process can set any values. An unprivileged process can only set
each ID to one of its current real, effective, or saved values. The
consequence: once a privileged process sets **all three** UIDs to 1000, it
cannot become root again. This is how daemons **drop privileges** permanently.

The order matters: **groups first, then GID, then UID**. After the UID is no
longer 0, the process no longer has permission to change its groups or GID.
Forgetting `setgroups()` is a classic bug: the process keeps root's
supplementary groups.

## Set-user-ID executables

How can an ordinary user change their password, when `/etc/shadow` is
writable only by root? The `passwd` program has the **set-user-ID (setuid)**
mode bit:

```console
$ ls -l /usr/bin/passwd
-rwsr-xr-x 1 root root 59976 ... /usr/bin/passwd
    ^ "s" instead of "x": set-user-ID
```

When a process `execve()`s a setuid file, the kernel sets the process's
**effective UID** (and saved set-user-ID) to the **file owner's** UID. The real
UID is unchanged. So `passwd` runs with real UID 1000 (it knows who you are)
and effective UID 0 (it is allowed to write `/etc/shadow`).

Setuid-root programs are powerful and dangerous: any bug in them is a path to
root. Two mechanisms can disable the setuid effect, and both reappear later:

- the `nosuid` mount option ignores setuid bits for files on that filesystem
  (Chapter 02);
- the `no_new_privs` process flag makes `execve()` ignore setuid bits for this
  process and all its descendants (Chapter 06).

## "root" is really two things

Historically, Unix had a single rule: **effective UID 0 bypasses permission
checks.** Root could read any file, kill any process, bind any port, load
kernel modules, and change the system clock.

Since Linux 2.2, the kernel splits root's power into separate
**capabilities**, such as `CAP_DAC_OVERRIDE` (bypass file permission checks),
`CAP_KILL` (send signals to any process), `CAP_NET_BIND_SERVICE` (bind ports
below 1024), and `CAP_SYS_ADMIN` (a very broad set including `mount`). A
process running as UID 0 normally has all of them, which is why root still
*appears* all-powerful. But the kernel's actual checks are mostly for a
capability, not for UID 0.

You can see this already:

```console
$ grep CapEff /proc/self/status
CapEff: 0000000000000000
$ sudo grep CapEff /proc/self/status
CapEff: 000001ffffffffff
```

This separation between "UID 0" and "having privileges" is what lets a
container process run as UID 0 while missing most of root's capabilities.
Chapter 05 explains the rules in detail. For this chapter, remember that
credentials include both the IDs and the capability sets.

## Why this matters for containers

- The `USER` setting of a container is applied the same way `login` applies
  your identity: in the gap before `execve()`, the runtime calls
  `setgroups()`, `setresgid()`, and `setresuid()`.
- Without a user namespace, UID 0 inside a container **is** UID 0 on the
  host kernel. What stops it from being fully privileged is mainly that the
  runtime removed most of its capabilities, plus namespaces, seccomp, and
  security modules. Understanding that stack is the goal of Chapters 03–06.
- Because the kernel only knows numbers, UID mismatches between the image's
  `/etc/passwd` and the host explain many permission problems with mounted
  volumes.
- Setuid binaries inside images are a privilege-escalation path. This is why
  container runtimes commonly offer `no_new_privs`.

## Evidence

Lab: [`lab-08-credentials`](../../labs/01-linux-process/lab-08-credentials/)

## Further Reading

- [`credentials(7)`](https://man7.org/linux/man-pages/man7/credentials.7.html)
  — the authoritative overview of process IDs, user and group IDs, and
  which syscalls use which ID. The single most important page for this section.
- [`setresuid(2)`](https://man7.org/linux/man-pages/man2/setresuid.2.html) and
  [`setgroups(2)`](https://man7.org/linux/man-pages/man2/setgroups.2.html) —
  the exact rules for unprivileged changes.
- [`execve(2)`](https://man7.org/linux/man-pages/man2/execve.2.html),
  paragraphs on set-user-ID and `nosuid` — how credentials change at exec.
- Hao Chen, David Wagner, Drew Dean,
  ["Setuid Demystified"](https://www.usenix.org/legacy/publications/library/proceedings/sec02/full_papers/chen/chen.pdf)
  (USENIX Security 2002) — explains why the older `setuid()` family is
  confusing and error-prone. Read it to appreciate why `setresuid()` exists.
- Kernel docs: [Credentials in Linux](https://docs.kernel.org/security/credentials.html)
  — how `struct cred` is shared, copied, and replaced. Useful background for
  runc and kernel source reading later.
