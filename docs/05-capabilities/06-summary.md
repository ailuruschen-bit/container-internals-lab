# 6. Chapter Summary: Root, Divided

## The model you should now have

Linux splits the historical all-powerful root into ~41 **capabilities**. The
kernel checks a specific capability (`ns_capable(ns, CAP_X)`) instead of
`euid == 0`. A thread has **five** capability sets; an executable file has
**three** fields; `execve()` recomputes the thread's sets from both. A user
namespace makes each check **relative to a namespace**.

```text
per thread:  Effective ⊆ Permitted        Bounding = ceiling
             Inheritable, Ambient (Ambient ⊆ Permitted ∩ Inheritable)

per file:    F(permitted), F(inheritable), F(effective bit)

at execve:   P'(perm) = (P(inh) & F(inh)) | (F(perm) & P(bnd)) | P'(amb)
             root rule: real/euid 0 ⇒ F(perm)=F(inh)=ALL ⇒ P'(perm)=P(inh)|P(bnd)

per check:   ns_capable(target_ns, CAP_X):
             effective in target_ns, or in an ancestor, or be the owner UID from the parent
```

## Updated properties table

| Property | After `fork()` | After `execve()` |
|---|---|---|
| Permitted, Effective, Inheritable | copied | recomputed by the formula |
| Bounding | copied | **preserved** (can only shrink, via `PR_CAPBSET_DROP`) |
| Ambient | copied | preserved for ordinary files; cleared for privileged files |
| Securebits, `no_new_privs` | copied | preserved |

## The sentences to remember

> The kernel checks capabilities, not UID 0. The **bounding set** is the real
> ceiling for a root process, because the root rule refills permitted from it at
> every `execve()`.

> Without a user namespace, container root is host UID 0. Its confinement is the
> dropped bounding set plus namespaces, cgroups, and seccomp; but its power over
> mounted host files comes from **ownership**, which capabilities cannot remove.

## What has *not* been explained yet

- **Syscall reduction.** Capabilities gate privileged operations, but a
  container can still *call* every system call, including large, rarely audited
  ones. `no_new_privs`, mentioned throughout this chapter, is defined and used in
  Chapter 06 together with **seccomp**.
- **LSMs.** AppArmor and SELinux add another, orthogonal layer
  (`CAP_MAC_ADMIN`/`CAP_MAC_OVERRIDE`). Chapter 06 touches on them.

## Self-check questions

1. Name the five thread capability sets and what each is for. Which one is the
   real limit for a root process, and why? (§2, §3)
2. Decode why `docker exec` into a root container yields a shell with the full
   default capability set, whatever the main process did with `capset()`. (§3)
3. Why does `--cap-add NET_BIND_SERVICE` usually have no effect for a container
   whose `USER` is not root? What would make it work? (§3)
4. A capability check is `ns_capable(net_ns->user_ns, CAP_NET_ADMIN)`. Explain
   who passes it: host root, container root without a user namespace, container
   root with one. (§4)
5. A container runs as root with the default set and a writable bind mount of
   `/etc`. Can it edit `/etc/passwd`? Which mechanism allows it, and which single
   change would stop it? (§4, §5)
6. List the four independent mechanisms that confine container root, and name one
   thing each cannot do. (§5)
7. Why is a kernel vulnerability the escape that all four layers struggle to
   contain, and which two mechanisms most reduce that risk? (§5)

## Next: Chapter 06 — seccomp and no_new_privs

Capabilities limit which privileged operations succeed. Chapter 06 limits which
**system calls** a process may make at all: seccomp strict and filter modes,
BPF filters and their actions, `no_new_privs` (used repeatedly in this chapter),
the default container seccomp profile, and how it interacts with capabilities.
