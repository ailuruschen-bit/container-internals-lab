# 8. Chapter Summary: A Container Is a Configured Process

## What you built

A ~350-line Go program that turns an ordinary process into a container by:

1. re-exec'ing itself into new namespaces (UTS, PID, mount, IPC, optional net and
   user);
2. giving it a private rootfs via `pivot_root` with `/proc`, `/sys`, `/dev`;
3. bounding its resources with a cgroup;
4. reducing its privileges with capability drops, `no_new_privs`, and a seccomp
   filter;
5. `execve`-ing the target program.

## The "what changed / what did not" ledger

| Stage | What changed | What did NOT |
|---|---|---|
| §1 re-exec | structure only | everything |
| §2 UTS+PID | own hostname; shell is PID 1 | `ps` still shows host (no new /proc) |
| §3 mount+rootfs | own `/`, own `/proc`; host fs absent | resources, privilege, syscalls |
| §4 net+ipc | isolated IPC; isolated (disconnected) net | resources, privilege; net not wired |
| §5 cgroups | bounded memory, PIDs | privilege, syscalls |
| §6 caps+seccomp | fewer capabilities, fewer syscalls | still UID 0 unless -userns; shared kernel |
| §7 -userns | container root = unprivileged host user | networking still isolated-only |

## The sentence, earned

> A container is a Linux process whose kernel-visible properties — namespaces,
> root filesystem, cgroup, capabilities, seccomp — were configured before
> `execve()`. Nothing about the process is special; the configuration is.

Everything from here up the stack (OCI, runc, containerd, Moby) is about
**describing** that configuration in a standard way, **applying** it correctly
and securely, **managing** many such processes, and **distributing** the
filesystems they use.

## Self-check questions

1. Why does `minic` re-exec itself instead of configuring the child directly in
   Go? Name two constraints that force it. (§1)
2. After §2 (UTS+PID) but before §3, `echo $$` is 1 yet `ps` shows host
   processes. Explain precisely why, and what §3 changes. (§2, §3)
3. List the `setupRootfs` steps and the chapter each comes from. Which one makes
   the host filesystem *absent* rather than hidden? (§3)
4. Why does the parent, not the child, write the cgroup limits and the
   user-namespace maps? (§5, §7)
5. Why are capabilities dropped and seccomp installed **last**, just before
   `execve`? (§6)
6. `minic` run as root confines the process with four layers but a writable host
   bind mount would still be dangerous. Why, and what does `-userns` change? (§6,
   §7)
7. Give three things `minic` does not do that runc does, and the chapter that
   covers each. (§7)

## Next: Chapter 10 — OCI

You configured a container with ad-hoc flags. Chapter 10 introduces the **Open
Container Initiative** specifications that standardize this: the runtime spec's
`config.json` (whose sections map one-to-one onto what `minic` did), the image
spec (Chapter 08's layers, formalized), the bundle, and the runtime lifecycle
that Chapter 11's runc implements.
