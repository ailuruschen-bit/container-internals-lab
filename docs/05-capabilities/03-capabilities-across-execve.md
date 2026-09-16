# 3. Capabilities Across execve

## The problem: what should a new program receive?

Chapter 01 §3 showed that `execve()` preserves most process properties.
Capabilities are the exception: they are **recalculated**. The kernel must
answer, for every `execve()`:

- Should a capability-holding shell pass its capabilities to `ls`? (Usually not:
  that would silently spread privilege.)
- Should executing a file with file capabilities grant them? (Yes, within
  limits.)
- Should executing a program as root grant all capabilities? (For backward
  compatibility, yes.)
- How can a supervisor deliberately give a non-root child program one
  capability?

## The transformation rules

Notation: `P` = thread sets before `execve()`, `P'` = after, `F` = file
capability fields. `&` is AND (intersection), `|` is OR (union).

```text
P'(ambient)     = (file is privileged) ? 0 : P(ambient)

P'(permitted)   = (P(inheritable) & F(inheritable))
                | (F(permitted)   & P(bounding))
                | P'(ambient)

P'(effective)   = F(effective) ? P'(permitted) : P'(ambient)

P'(inheritable) = P(inheritable)          (unchanged)
P'(bounding)    = P(bounding)             (unchanged)
```

A file is **privileged** if it has file capabilities or is setuid/setgid.

Read the permitted rule line by line:

1. A capability passes from the old process **only** if it is inheritable in
   **both** the thread and the file. Ordinary files have empty `F(inheritable)`,
   so this line usually contributes nothing.
2. The file's own permitted capabilities are granted, **limited by the bounding
   set**.
3. Ambient capabilities are kept for non-privileged files.

The consequence of line 1: **an ordinary process that executes an ordinary
program loses all capabilities**, unless they are ambient. A root-less daemon
with `CAP_NET_ADMIN` that runs `ip` gets an `ip` process with nothing.

## The root special case

For compatibility with traditional Unix, root gets special treatment. If the
process's **real or effective UID is 0** at `execve()` (and the securebit
`SECBIT_NOROOT` is not set), the kernel pretends that the file has:

- `F(inheritable)` = all capabilities;
- `F(permitted)` = all capabilities;
- `F(effective)` = set, if the **effective** UID is 0.

Substituting into the rules:

```text
P'(permitted) = (P(inheritable) & ALL) | (ALL & P(bounding)) | ...
              = P(inheritable) | P(bounding)
P'(effective) = P'(permitted)
```

So **root executing any program gets every capability in its bounding set**.
That is why the bounding set, not the permitted set, is the real limit for
root processes, and why container runtimes must reduce it (Lab 01, Part D).

## Capabilities when UIDs change

Chapter 01 §7 described dropping privileges with `setresuid()`. Capabilities
follow UID changes by these rules (unless disabled with securebits):

| UID transition | Effect on capabilities |
|---|---|
| any of real/effective/saved was 0, and **all** become non-zero | **permitted, effective, and ambient are cleared** |
| effective UID changes from 0 to non-zero | effective cleared |
| effective UID changes from non-zero to 0 | permitted copied to effective |
| filesystem UID changes 0 ↔ non-zero | file-related effective capabilities (`CHOWN`, `DAC_OVERRIDE`, `DAC_READ_SEARCH`, `FOWNER`, `FSETID`, `LINUX_IMMUTABLE`, `MKNOD`, `MAC_OVERRIDE`) cleared or restored |

That is the mechanism behind Chapter 01 Lab 08: `setpriv --reuid=4242` cleared
every capability.

### Keeping capabilities while leaving UID 0

A program (or a container runtime) that must switch to a non-root user **and**
keep some capabilities uses:

1. `prctl(PR_SET_KEEPCAPS, 1)` before `setresuid()`: the **permitted** set is
   kept (effective is still cleared), then
2. `capset()` to put the wanted capabilities back into effective, and
3. **ambient** capabilities, if they must survive the following `execve()` of a
   normal program.

## Ambient capabilities (Linux 4.3+)

Before ambient capabilities, giving a non-root program a capability required
either file capabilities on that program, or matching inheritable bits on both
thread and file. Scripts and interpreters made this impractical: you cannot put
file capabilities on every Python script, and putting them on `/usr/bin/python3`
grants them to every Python program.

The **ambient set** solves this. A capability in the ambient set:

- must also be in both permitted and inheritable;
- is added to permitted and effective at `execve()` of an **unprivileged** file;
- is cleared at `execve()` of a setuid/setgid or file-capability program (so that
  combination cannot be abused);
- is preserved across `fork()`.

```bash
# Run python as UID 1000, allowed to bind port 80, without file capabilities:
sudo setpriv --reuid=1000 --regid=1000 --clear-groups \
     --inh-caps=+net_bind_service --ambient-caps=+net_bind_service \
     python3 -m http.server 80
```

systemd exposes the same mechanism as `AmbientCapabilities=` in unit files.

## Securebits

Per-thread flags, set with `prctl(PR_SET_SECUREBITS, ...)` (requires
`CAP_SETPCAP`), disable the root compatibility rules. Each has a `_LOCKED`
variant that makes the choice irreversible:

| Bit | Effect |
|---|---|
| `SECBIT_NOROOT` | UID 0 no longer gets the "all capabilities" special case at `execve()` |
| `SECBIT_NO_SETUID_FIXUP` | UID transitions no longer modify capability sets |
| `SECBIT_KEEP_CAPS` | like `PR_SET_KEEPCAPS` |
| `SECBIT_NO_CAP_AMBIENT_RAISE` | ambient capabilities cannot be raised |

With `SECBIT_NOROOT | SECBIT_NO_SETUID_FIXUP` (locked), UID 0 becomes an ordinary
user whose privileges are **exactly** its capability sets: a pure capability
system.

## `no_new_privs`, briefly

`prctl(PR_SET_NO_NEW_PRIVS, 1)` (Chapter 06) guarantees that `execve()` never
grants **new** privileges: setuid bits and file capabilities are ignored, so
`P'(permitted)` cannot exceed the old permitted set. It is inherited and cannot
be unset. It complements the bounding set: the bounding set limits **which**
capabilities can ever be gained, `no_new_privs` prevents gaining any at all.

## Why this matters for containers

A runtime such as runc applies the OCI `process.capabilities` and `process.user`
in an order dictated by the rules above:

```text
(still root, full capabilities)
1. drop from the bounding set everything not in capabilities.bounding   prctl(PR_CAPBSET_DROP)
2. prctl(PR_SET_KEEPCAPS, 1)                                            keep permitted across setuid
3. setgroups / setresgid / setresuid to process.user                    Chapter 01 §7
4. capset(): permitted, effective, inheritable = configured sets        restore after the UID change
5. prctl(PR_CAP_AMBIENT_RAISE) for each capabilities.ambient entry
6. prctl(PR_SET_NO_NEW_PRIVS) if configured, seccomp                    Chapter 06
7. execve(application)
      → root user:     P' = I | bounding     (root rule)
      → non-root user: P' = ambient          (only ambient capabilities survive)
```

Two practical consequences:

- A container running as a **non-root user** keeps **no** capabilities after
  `execve()` unless they are ambient. Docker and Kubernetes do not set ambient
  capabilities by default, so `--cap-add NET_BIND_SERVICE` has **no effect** for a
  container whose `USER` is not root, unless the binary has file capabilities
  (and `no_new_privs` is off). This surprises many users.
- A container running as **root** gets every capability in its bounding set at
  every `execve()`, including shells spawned by `docker exec`. The bounding set is
  the real boundary.

## Evidence

Lab: [`lab-03-capabilities-across-execve`](../../labs/05-capabilities/lab-03-capabilities-across-execve/)

## Further Reading

- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html),
  sections "Transformation of capabilities during execve()", "Capabilities and
  execution of programs by root", "Effect of user ID changes on capabilities",
  and "The securebits flags". The formulas above are taken from there.
- LWN, ["Inheriting capabilities"](https://lwn.net/Articles/632520/)
  (2015) — the problem ambient capabilities solve, and the design discussion.
- [`setpriv(1)`](https://man7.org/linux/man-pages/man1/setpriv.1.html) — options
  `--inh-caps`, `--ambient-caps`, `--bounding-set`, `--securebits`,
  `--no-new-privs`; a command-line interface to every rule in this section.
- systemd, [`systemd.exec(5)`](https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html),
  `CapabilityBoundingSet=` and `AmbientCapabilities=` — how a production service
  manager applies the same rules.
- Kernel source: [`security/commoncap.c`](https://elixir.bootlin.com/linux/v6.12/source/security/commoncap.c),
  `cap_bprm_creds_from_file()` and `cap_emulate_setxuid()` — the formulas and UID
  transition rules in C.
