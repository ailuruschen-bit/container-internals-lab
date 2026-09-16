# Lab 03 — Argument Matching and Its Limits

## Goal

Produce evidence that:

1. a seccomp filter can allow or block a syscall based on an **argument value**;
2. blocking `CLONE_NEWUSER` stops user-namespace creation while other
   `unshare`/`clone` uses still work;
3. `clone3` must be handled specially because its flags live in a struct the
   filter cannot read;
4. seccomp cannot filter on a **path** argument.

## Prerequisites

- Linux VM with `gcc`, `libseccomp-dev`, `util-linux` (`unshare`), `strace`.
- Unprivileged user namespaces available (Chapter 03 Lab 07 check) to see Part B
  clearly.
- Read: [3. Actions and arguments](../../../docs/06-seccomp/03-actions-and-arguments.md).

## Build

```bash
gcc -Wall -o argfilter argfilter.c -lseccomp
```

## Experiment

### Part A — A new user namespace is blocked

**Predict first.** Under `argfilter`, will `unshare -U` (new user namespace)
succeed? Will `unshare -u` (new **UTS** namespace, needs privilege) behave
differently?

```bash
./argfilter /bin/sh -c 'unshare -U --map-root-user echo "userns ok"; echo "userns exit: $?"'
./argfilter /bin/sh -c 'unshare -m echo "mountns ok" 2>&1; echo "mountns exit: $?"'
```

### Part B — Compare without the filter

```bash
unshare -U --map-root-user echo "userns ok without filter"; echo "exit: $?"
```

### Part C — See the argument rule in strace

```bash
strace -f -e trace=clone,clone3,unshare ./argfilter /bin/sh -c 'unshare -U true' 2>&1 | grep -E 'unshare|clone'
```

### Part D — clone3 fallback

```bash
strace -f -e trace=clone,clone3 ./argfilter /bin/true 2>&1 | grep -E 'clone' | head
```

### Part E — seccomp cannot filter on a path

There is no rule you can add to `argfilter` that allows `open` only under
`/tmp`. Demonstrate that the syscall sees only an address, not the string:

```bash
strace -e trace=openat /bin/cat /etc/hostname 2>&1 | grep hostname
```

Look at the first argument of `openat`: the path appears in `strace` output only
because `strace` (a `ptrace` tracer) dereferences it; the seccomp filter runs
earlier and sees just a pointer value.

## Expected observations

**Part A.** `unshare -U` fails: `unshare: unshare failed: Operation not
permitted`, exit non-zero. `unshare -m` (mount namespace) either succeeds
(prints `mountns ok`) if run with privilege, or fails with a *different* error
(`Operation not permitted` because a mount namespace needs `CAP_SYS_ADMIN`), but
**not** because of the user-namespace rule.

**Part B.** Without the filter, `unshare -U` prints `userns ok without filter`,
exit 0.

**Part C.** The trace shows `unshare(CLONE_NEWUSER) = -1 EPERM (Operation not
permitted)`. If `unshare` first tries a combined flag set, you see the EPERM on
the call that includes `CLONE_NEWUSER`.

**Part D.** On modern glibc, you may see `clone3(...) = -1 ENOSYS` followed by a
`clone(...)` call: glibc fell back from `clone3` to `clone` because the filter
returned `ENOSYS`. This is why the filter returns `ENOSYS` (not `EPERM`) for
`clone3`.

**Part E.** `strace` prints the path `"/etc/hostname"` as the second argument of
`openat`, but that is `strace` reading the tracee's memory. A seccomp filter
sees only `args[1]` = a pointer value, which is not useful for a path decision.

## Why this happens

- **A, C.** `SCMP_CMP_MASKED_EQ` compares `(args[0] & CLONE_NEWUSER)` to
  `CLONE_NEWUSER`; only calls that set the bit match the `ERRNO` rule.
- **D.** The filter cannot read the `clone_args` struct that `clone3` points to,
  so it blocks the whole syscall with `ENOSYS`, prompting the libc fallback to
  `clone`, whose flags **are** in a register and can be filtered.
- **E.** cBPF cannot dereference user pointers safely (TOCTOU and faulting), so
  path arguments are invisible to seccomp.

## Connection to containers

- Blocking `CLONE_NEWUSER` was, for years, part of the default Docker profile
  (to prevent nested user namespaces); this argument rule is exactly that.
- Part E is why "restrict which files a container can open" is done with mount
  namespaces, read-only mounts, and LSMs (AppArmor/SELinux), never with seccomp.
- Part D is why container profiles must decide carefully between `EPERM` and
  `ENOSYS`: the wrong choice breaks libc/Go syscall fallbacks.

## Questions to think about

1. Why is returning `ENOSYS` for `clone3` safer for compatibility than `EPERM`?
2. Give two mechanisms from earlier chapters that *can* restrict which paths a
   container opens, and say why each works where seccomp cannot.
3. How could a runtime allow a container to call `mount()` for one specific,
   safe filesystem without granting `CAP_SYS_ADMIN`? (Hint: section 3's
   `SCMP_ACT_NOTIFY`.)
