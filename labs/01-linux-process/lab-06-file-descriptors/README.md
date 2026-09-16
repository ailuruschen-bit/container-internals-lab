# Lab 06 — File Descriptors: Tables, Shared Offsets, and close-on-exec

## Goal

Produce evidence that:

1. a file descriptor points to an open file description whose offset is
   shared between parent and child after `fork()`;
2. descriptors without close-on-exec survive `execve()`, and descriptors with
   it do not;
3. an inherited descriptor grants access that does not depend on path
   permissions at the time of use.

## Prerequisites

- Linux VM with `gcc` and `sudo`.
- Read: [5. File descriptors](../../../docs/01-linux-process/05-file-descriptors.md).

## Background

The per-process fd table maps numbers to open file descriptions. `fork()`
copies the table but not the open file descriptions. `execve()` closes every
descriptor with `FD_CLOEXEC`.

## Experiment

### Part A — Shared offset and close-on-exec

**Predict first.**

1. The child reads 5 bytes from `fd_a`, then exits. The parent then reads 5
   bytes from `fd_a`. Which bytes does the parent get?
2. Which of `fd_a` and `fd_b` will `ls` see in `/proc/self/fd`?

```bash
gcc -Wall -o fd_inherit fd_inherit.c
./fd_inherit
```

### Part B — Descriptors in your shell

```bash
ls -l /proc/$$/fd
exec 7</etc/hostname          # open fd 7 in the current shell
ls -l /proc/$$/fd
bash -c 'ls -l /proc/self/fd; cat <&7'
exec 7<&-                     # close fd 7
```

### Part C — An inherited descriptor bypasses a later permission check

This part shows why leaked descriptors are dangerous.

```bash
sudo sh -c 'echo "secret" > /root/lab-secret.txt; chmod 600 /root/lab-secret.txt'
cat /root/lab-secret.txt                                  # as your user
sudo bash -c 'exec 9</root/lab-secret.txt; exec setpriv --reuid=$SUDO_UID --regid=$SUDO_GID --clear-groups bash -c "id -u; cat /root/lab-secret.txt; cat <&9"'
sudo rm /root/lab-secret.txt
```

The root shell opens the file as fd 9, then `setpriv` drops to your user ID
and `execve()`s an unprivileged `bash`. `setpriv` is part of `util-linux`.

**Predict first.** The unprivileged `bash` runs two `cat` commands. Which one
succeeds?

## Expected observations

**Part A.**

```text
parent  pid=7000 fd_a=3 (no CLOEXEC) fd_b=4 (CLOEXEC)
child   read 5 bytes "01234" from fd 3, offset now 5
parent  read 5 bytes "56789" from fd 3, offset now 10
child   pid=7002 exec'ing ls; expect fd 3 present, fd 4 absent
lr-x------ 1 user user 64 ... 0 -> /dev/pts/0
l-wx------ 1 user user 64 ... 1 -> /dev/pts/0
lrwx------ 1 user user 64 ... 2 -> /dev/pts/0
lr-x------ 1 user user 64 ... 3 -> /tmp/fd_inherit_data.txt
lr-x------ 1 user user 64 ... 4 -> /proc/7002/fd
```

The last line is **not** `fd_b`. It is the directory that `ls` itself opened to
list `/proc/self/fd`: the lowest free number is 4, precisely because `fd_b`
was closed by `execve()`.

**Part B.** After `exec 7<`, the shell's table contains `7 -> /etc/hostname`.
The child `bash` lists fd 7 as well, and `cat <&7` prints the hostname.

**Part C.** `id -u` prints your normal user ID. The first `cat` fails with
`Permission denied`. `cat <&9` prints `secret`.

## Why this happens

- **A.** Both processes' fd 3 entries point to one `struct file` holding the
  offset. The child's `read()` advanced it to 5. `O_CLOEXEC` set
  `FD_CLOEXEC` on `fd_b`, so `execve()` closed it before `ls` started.
- **B.** A shell's `exec N<file` opens a descriptor without close-on-exec,
  deliberately, so child programs inherit it.
- **C.** Permission checks happen at `open()`. The unprivileged program never
  opens the file; it reads from an already-open file description it inherited.

## Connection to containers

- Part C is the essence of descriptor-leak container escapes. Replace "root
  shell" with "container runtime", `setpriv` with "all container setup", and
  `/root/lab-secret.txt` with "a host directory". Namespaces and `pivot_root`
  change how *paths* are resolved; they cannot revoke a descriptor that is
  already open.
- This is why runc explicitly closes or marks close-on-exec every descriptor
  it does not intend to pass, just before executing the container process,
  and why CVE-2024-21626 was serious.
- The fds 0, 1, and 2 of a container process are prepared the same way as in
  Part B: a parent opens them and the child inherits them.

## Questions to think about

1. In Part A, if the child had used `open()` itself instead of inheriting
   `fd_a`, what would the parent have read?
2. Why must `O_CLOEXEC` be set atomically in `open()`, rather than with a
   separate `fcntl()` call, in a multithreaded program such as a JVM? (Hint:
   what if another thread calls `fork()` + `execve()` between the two calls?)
3. Java's `ProcessBuilder` passes only stdin, stdout, and stderr to the child.
   Which mechanism from this lab does it rely on?
4. List the descriptors of a running `dockerd` or `containerd` process with
   `sudo ls -l /proc/<pid>/fd`. Which kinds of objects (sockets, pipes,
   files) do you see?
