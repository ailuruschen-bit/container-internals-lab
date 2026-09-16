# Lab 08 — Process Credentials, Privilege Dropping, and setuid

## Goal

Produce evidence that:

1. the kernel stores and checks numeric IDs, not user names;
2. a privileged process can change its credentials before `execve()`, and the
   new program inherits them;
3. a set-user-ID executable changes the effective and saved UID, but not the
   real UID;
4. UID 0 normally comes with a full set of capabilities, and those are also
   part of a process's credentials.

## Prerequisites

- Linux VM with `gcc`, `util-linux` (`setpriv`), and `sudo`.
- Read: [7. Credentials](../../../docs/01-linux-process/07-credentials.md).

This lab creates a temporary setuid-root binary. Do it only in a disposable VM,
and delete it at the end.

## Background

`setpriv` is a small tool from util-linux that changes credentials
(`setgroups`, `setresgid`, `setresuid`) and then `execve()`s a command. It is
the create/configure/execute pattern packaged as a command.

## Experiment

### Part A — Your own credentials

```bash
gcc -Wall -o creds creds.c
id
./creds
grep -E '^(Uid|Gid|Groups)' /proc/self/status
```

### Part B — Root, and root's capabilities

```bash
sudo ./creds
grep CapEff /proc/self/status
sudo grep CapEff /proc/self/status
```

### Part C — Dropping privileges in the gap

**Predict first.** Root runs `setpriv` to become UID/GID 4242, a number that
does not exist in `/etc/passwd`. What will `id` print? Can the resulting
process create a file?

UID 4242 may not be allowed to enter your home directory (many distributions
create home directories with mode `750`), so copy the program to `/tmp` first:

```bash
cp creds /tmp/creds
sudo setpriv --reuid=4242 --regid=4242 --clear-groups id
sudo setpriv --reuid=4242 --regid=4242 --clear-groups /tmp/creds
sudo setpriv --reuid=4242 --regid=4242 --clear-groups touch /tmp/owned-by-4242
ls -l /tmp/owned-by-4242
ls -ln /tmp/owned-by-4242
```

Now check that the drop is permanent:

```bash
sudo setpriv --reuid=4242 --regid=4242 --clear-groups sudo -n true; echo "exit code: $?"
sudo setpriv --reuid=4242 --regid=4242 --clear-groups bash -c 'grep -E "^(Uid|CapEff)" /proc/self/status'
```

### Part D — Forgetting supplementary groups

```bash
sudo setpriv --reuid=4242 --regid=4242 --keep-groups /tmp/creds
```

Compare the supplementary groups with Part C.

### Part E — A set-user-ID executable

First check that the current directory allows setuid execution:

```bash
findmnt -no OPTIONS -T .     # must NOT contain "nosuid"
```

```bash
cp creds creds-setuid
sudo chown root:root creds-setuid
sudo chmod u+s creds-setuid
ls -l creds-setuid
./creds-setuid
```

**Predict first.** Which of the four UIDs will be 0? Will `open(/etc/shadow)`
succeed?

Then run it again with the `no_new_privs` flag set by `setpriv`:

```bash
setpriv --no-new-privs ./creds-setuid
```

and clean up:

```bash
sudo rm -f creds-setuid /tmp/owned-by-4242 /tmp/creds
```

## Expected observations

**Part A.** `id` shows names and numbers, for example
`uid=1000(alice) gid=1000(alice) groups=1000(alice),27(sudo)`. `creds` prints
the same numbers: all UIDs equal, and `open(/etc/shadow): Permission denied`.

**Part B.** `sudo ./creds`: all UIDs are 0 and `open(/etc/shadow): OK`.
`CapEff` is `0000000000000000` without `sudo` and something like
`000001ffffffffff` with `sudo` (the exact value depends on the kernel's number
of capabilities).

**Part C.** `id` prints `uid=4242 gid=4242 groups=4242` without names, because
no name exists. `creds` shows all UIDs as 4242. `ls -l` shows the owner as
the number `4242`. `sudo -n true` fails (`exit code: 1`), with a message
such as "you do not exist in the passwd database" or "a password is required".
`/proc/self/status` shows `Uid: 4242 4242 4242 4242` and `CapEff` all zeros.

**Part D.** With `--keep-groups`, the supplementary group list contains `0`
(root's group) even though the UID is 4242.

**Part E.** `ls -l` shows `-rwsr-xr-x 1 root root`. `./creds-setuid` prints
`uid: real=1000 effective=0 saved=0 fs=0`, and `open(/etc/shadow): OK`.
With `setpriv --no-new-privs`, all UIDs are 1000 again and the open fails with
`Permission denied`.

## Why this happens

- **A, C.** `id` translates numbers into names with `/etc/passwd`; where no
  entry exists, it prints numbers only. The kernel stores the number in the
  inode as owner, with or without a name.
- **C.** `setpriv` called `setgroups([])`, `setresgid(4242,4242,4242)`, and
  `setresuid(4242,4242,4242)` while it was still root, then `execve()`d the
  command, which inherited the credentials. With no saved UID of 0, nothing
  can restore root. `sudo` itself is setuid-root, but it refuses to run for a
  user without a valid account and password.
- **C.** When all UIDs change from 0 to non-zero, the kernel also clears the
  permitted and effective capability sets (this rule is covered in Chapter 05).
- **D.** Groups are a separate credential. Changing UID and GID does not touch
  them.
- **E.** At `execve()`, the kernel saw the setuid bit and root ownership and set
  effective and saved UID to 0. With `no_new_privs`, the kernel ignores the
  setuid bit during `execve()`.

## Connection to containers

- Part C is precisely what a runtime does for a container's configured user:
  `setgroups` → `setresgid` → `setresuid` → `execve`. You will find these calls
  in runc's source code in Chapter 11.
- Part C also shows why a numeric UID in a container can own host files: the
  kernel sees 4242 regardless of which `/etc/passwd` is visible.
- Part D is a real class of misconfiguration: an application that drops UID but
  keeps privileged supplementary groups.
- Part E demonstrates both the risk of setuid binaries in container images and
  the defense (`no_new_privs`) that runtimes can enable (Chapter 06).

## Questions to think about

1. In Part E, why does the program keep the real UID 1000 instead of changing
   everything to 0?
2. In Part C, the process lost all capabilities when leaving UID 0. How could
   a program keep one capability, such as `CAP_NET_BIND_SERVICE`, after
   dropping to a normal UID? (You will answer this properly in Chapter 05.)
3. A container runs as UID 0 without a user namespace, and a host directory is
   mounted into it. Which of the four permission-check steps applies when it
   writes a file there? What prevents or allows the write?
4. Why is the order "groups, then GID, then UID" required when dropping
   privileges?
