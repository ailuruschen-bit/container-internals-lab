# 2. seccomp Modes and BPF Filters

## The problem

A process wants to tell the kernel: "from now on, I intend to use only these
system calls; if I try any other, stop me." This must be enforced by the kernel
(a compromised process cannot be trusted to restrain itself), it must apply to
the process and all its descendants, and it must survive `execve()` so it can be
set up *before* running the target program, exactly the create-configure-execute
pattern from Chapter 01 §3.

That mechanism is **seccomp**.

## Strict mode: the original seccomp

The first version of seccomp (Linux 2.6.12, 2005) has one rule set. After:

```c
prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT);
```

the process may call only **four** system calls: `read()`, `write()`,
`_exit()`, and `sigreturn()`. Any other syscall immediately kills the process
with `SIGKILL`. It was designed for running untrusted computation on
already-open file descriptors (a "compute grid" use case). It is too restrictive
for general programs and is rarely used directly, but it introduces the model:
the kernel checks each syscall against a fixed policy.

## Filter mode: seccomp-BPF

The version everything uses today (Linux 3.5, 2012) is **filter mode**, also
called **seccomp-BPF**. Instead of a fixed list, the process supplies a
**program** that the kernel runs on **every system call** to decide what to do.

```c
prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
/* or the dedicated syscall, which can also apply flags: */
seccomp(SECCOMP_SET_MODE_FILTER, flags, &prog);
```

### What the filter program is

The program is written in **classic BPF** (cBPF), the same tiny instruction set
originally created for packet filtering (`tcpdump`). This is *not* the newer
eBPF used elsewhere in the kernel; seccomp uses the older, simpler, more
constrained classic BPF, because a syscall filter must be safe, fast, and
free of side effects.

The kernel runs the filter with a fixed input structure describing the syscall:

```c
struct seccomp_data {
    int   nr;                    /* the system call number */
    __u32 arch;                  /* AUDIT_ARCH_X86_64, AUDIT_ARCH_AARCH64, ... */
    __u64 instruction_pointer;   /* where the syscall was made */
    __u64 args[6];               /* the six syscall arguments */
};
```

The filter reads fields of this structure, does comparisons, and **returns an
action value** (section 3) that tells the kernel to allow the call, fail it,
kill the process, and so on.

### Why the architecture check comes first

`nr` (the syscall number) is only meaningful together with `arch`. Syscall
numbers differ between architectures, and on x86-64 a process can also make
32-bit syscalls (via the `int 0x80` / x32 paths) that have **different numbers**.
A filter that blocks `execve` (number 59 on x86-64) but forgets to check `arch`
can be bypassed by calling the 32-bit `execve` (number 11). Every correct filter
therefore **first checks `arch`** and denies unexpected architectures. This is a
classic seccomp bug, and the reason runtimes disable non-native architectures.

### The shape of a filter

Conceptually, a syscall-allow-list filter does:

```text
if data.arch != AUDIT_ARCH_X86_64:  return KILL_PROCESS
if data.nr == read:                 return ALLOW
if data.nr == write:                return ALLOW
if data.nr == execve:               return ALLOW
... (the allowed calls)
return ERRNO(EPERM)                  # everything else
```

Writing cBPF by hand is tedious, so almost everyone uses **libseccomp**, a
library that compiles a high-level rule list into a cBPF program:

```c
scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ERRNO(EPERM));   /* default action */
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
seccomp_load(ctx);   /* handles arch checks and calls seccomp() for you */
```

libseccomp also handles the multi-architecture problem, syscall-number
resolution across kernels, and the ordering of comparisons. Container runtimes
and language sandboxes use it (runc through the Go binding
`github.com/seccomp/libseccomp-golang`).

## Rules, inheritance, and permission

- **Filters stack.** Installing a second filter does not replace the first; both
  run, and the **most restrictive** action wins. A process can only ever add
  restrictions, never remove them.
- **Inherited and preserved.** Filters are copied across `fork()` and preserved
  across `execve()`, so a runtime installs the filter and then `execve()`s the
  application under it.
- **Permission.** Installing a filter needs `CAP_SYS_ADMIN`, **or**
  `no_new_privs` must be set (section 1). Runtimes take the second path so the
  container process needs no capability for seccomp.
- **Cost.** The filter runs on every syscall, adding a small, fixed overhead.
  For syscall-heavy workloads this is measurable but usually minor; the kernel
  offers a bitmap fast path for simple allow/deny filters.

## Why this matters for containers

- A container's seccomp profile is a filter installed by the runtime just before
  `execve()`, exactly like capability dropping. The OCI configuration expresses
  it as `linux.seccomp` (default action, architectures, and per-syscall rules),
  which the runtime compiles with libseccomp.
- Blocking a syscall usually returns an **error** (`EPERM`), so a container sees
  the same failure you saw for a missing capability in Chapter 05. Recognizing
  "this failed with `EPERM` and it is a syscall the profile blocks" is a key
  debugging skill.
- The architecture check is why container runtimes matter about the host
  architecture and why 32-bit syscall paths are typically blocked.

## Evidence

Lab: [`lab-02-seccomp-filter`](../../labs/06-seccomp/lab-02-seccomp-filter/)

## Further Reading

- Kernel docs: [Seccomp BPF (SECure COMPuting with filters)](https://docs.kernel.org/userspace-api/seccomp_filter.html)
  — the authoritative reference: `seccomp_data`, the two modes, filter stacking,
  the architecture pitfall, and every action. The core source for this chapter.
- [`seccomp(2)`](https://man7.org/linux/man-pages/man2/seccomp.2.html) — the
  syscall, flags (`SECCOMP_FILTER_FLAG_*`), and return-value precedence.
- [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html),
  `PR_SET_SECCOMP` and `PR_GET_SECCOMP`.
- libseccomp: [seccomp_init(3)](https://man7.org/linux/man-pages/man3/seccomp_init.3.html)
  and [seccomp_rule_add(3)](https://man7.org/linux/man-pages/man3/seccomp_rule_add.3.html)
  — the high-level API used to build real profiles.
- [`bpf(2)`](https://man7.org/linux/man-pages/man2/bpf.2.html) and
  [`bpfc(1)`](https://www.man7.org/linux/man-pages/man1/bpfc.1.html) — background
  on classic vs extended BPF; useful to understand why seccomp uses cBPF.
