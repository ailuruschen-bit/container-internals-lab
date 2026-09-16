# 1. no_new_privs

## The problem

Chapter 01 §7 and Chapter 05 §3 showed that `execve()` can **grant** privileges:
a setuid bit changes the effective UID to the file owner, and file capabilities
add to the permitted set. This is necessary for tools like `passwd`, but it is
dangerous in two situations:

1. A sandbox wants to run untrusted code and be **certain** it can never gain
   privileges, even if the code executes a setuid-root binary that happens to
   exist in the filesystem.
2. A seccomp filter wants to restrict a process, but a restricted process must
   not be able to escape by executing a setuid program that runs with more
   privileges than the filter author intended. Without a guarantee against
   privilege gain, seccomp filters would be a way to trick setuid programs.

## The mechanism

`no_new_privs` is a per-thread flag set with:

```c
#include <sys/prctl.h>
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
```

Once set, it has three properties:

- **One-way.** It cannot be unset. `prctl(PR_GET_NO_NEW_PRIVS)` reads it back.
- **Inherited.** It is copied across `fork()` and preserved across `execve()`,
  and every descendant has it.
- **Blocks privilege gain at `execve()`.** With the flag set, `execve()` ignores
  the setuid/setgid bits (the effective UID does not change to the file owner)
  and ignores file capabilities (`F(permitted)` contributes nothing). The
  process can still execute the program; it just does not gain anything.

It is visible in `/proc/<pid>/status`:

```text
NoNewPrivs:     1
```

Note what it does **not** do: it does not drop capabilities the process already
has, and it does not block the root special case for a process that is *already*
UID 0 (that is what securebits and dropping the bounding set are for,
Chapter 05). It only prevents `execve()` from *raising* privileges.

## Relationship to seccomp

Installing a seccomp **filter** requires either `CAP_SYS_ADMIN` **or**
`no_new_privs` already set. The reason is exactly point 2 above: an unprivileged
process without `CAP_SYS_ADMIN` must prove it cannot use a filter to subvert a
setuid program, and setting `no_new_privs` is that proof. Container runtimes and
sandboxes set `no_new_privs` first, then install the filter, so no capability is
needed for the seccomp step itself.

## Why this matters for containers

- Kubernetes `securityContext.allowPrivilegeEscalation: false` sets exactly this
  flag. It is `true` by default, except it is forced to `false` when the
  container is unprivileged and `no_new_privs`-compatible in the Restricted Pod
  Security Standard.
- Docker sets it with `--security-opt no-new-privileges`.
- The OCI configuration field is `process.noNewPrivileges`.
- With the flag set, setuid binaries and file-capability binaries inside an
  image stop working as privilege-escalation vectors (Chapter 05 Lab 02 Part E,
  Lab 03 confirmed this). This is usually desirable, but it can break images
  that rely on a setuid helper (for example some `ping` implementations, or
  `sudo`-based entrypoints).

## Evidence

Lab: [`lab-01-no-new-privs`](../../labs/06-seccomp/lab-01-no-new-privs/)

## Further Reading

- Kernel docs: [No New Privileges](https://docs.kernel.org/userspace-api/no_new_privs.html)
  — the two-page authoritative description, including the exact interaction with
  `execve()` and seccomp.
- [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html),
  `PR_SET_NO_NEW_PRIVS` and `PR_GET_NO_NEW_PRIVS`.
- [`execve(2)`](https://man7.org/linux/man-pages/man2/execve.2.html), the
  paragraph listing when set-user-ID and file capabilities are ignored.
- [`setpriv(1)`](https://man7.org/linux/man-pages/man1/setpriv.1.html),
  `--no-new-privs` — the command-line way to set it (used in Chapter 05 labs).
