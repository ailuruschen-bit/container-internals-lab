# 4. Capabilities and User Namespaces

Chapter 03 §7 introduced the idea that a process can have capabilities *in* a
user namespace. With the capability sets and `execve()` rules now defined, the
model can be stated precisely.

## The rule

Every capability check names a **target user namespace**:

```c
ns_capable(target_user_ns, CAP_X)
```

A process has capability `CAP_X` for that check if and only if one of these is
true:

1. the process is **in** `target_user_ns` and `CAP_X` is in its **effective** set;
2. the process is in an **ancestor** user namespace of `target_user_ns`, and has
   `CAP_X` in its effective set there;
3. the process is in the **parent** user namespace of `target_user_ns` and its
   effective UID is the **owner** of `target_user_ns` (the UID that created it):
   then it has **all** capabilities in `target_user_ns`.

`capable(CAP_X)` is simply `ns_capable(&init_user_ns, CAP_X)`: the check against
the **initial** user namespace.

```text
init_user_ns                           process H: host root, CapEff=all
│                                       → has CAP_X in init_user_ns and in every descendant (rule 2)
│
└── user ns U (owner UID 1000)         process C: UID 0 inside U, CapEff=all
     │                                  → has CAP_X in U and its descendants
     │                                  → has NOTHING in init_user_ns
     └── user ns V (owner UID 0 in U)
```

Process owned by UID 1000 in `init_user_ns`, even with `CapEff=0`, has all
capabilities in U by rule 3. That is why you could write U's ID maps in Chapter 03
Lab 07 without `sudo`.

## Which namespace is the target?

The kernel chooses the target based on **what is being acted on**:

| Operation | Target user namespace | Consequence |
|---|---|---|
| `sethostname()` | owner of the caller's UTS namespace | root in U can set the hostname of a UTS namespace created inside U |
| Configure a network interface | owner of the network namespace | root in U can configure its own net namespace, not the host's |
| `mount()` | owner of the caller's mount namespace, **plus** filesystem-specific rules | only `FS_USERNS_MOUNT` filesystems (tmpfs, proc, sysfs, overlay, fuse, ...) |
| Bind port < 1024 | owner of the network namespace | root in U can bind port 80 in its own net namespace |
| Override file permissions (`CAP_DAC_OVERRIDE`) | the user namespace in which the file's owner UID and GID are **mapped** | root in U can override permissions only on files whose owner is mapped into U |
| Load a kernel module, set the clock, reboot, raw I/O | `init_user_ns` | never possible from inside U |
| Create device nodes (`CAP_MKNOD`) | `init_user_ns` (for real devices) | never possible from inside U |

## File capabilities in user namespaces

A file capability created inside a user namespace records the **namespace root
UID** (version 3 xattr, Linux 4.14+). It only takes effect for processes in a user
namespace where that UID is root. An image layer created by a rootless build
cannot grant capabilities on the host.

## Capabilities of the process that creates a user namespace

- The child of `clone(CLONE_NEWUSER)` (or the caller of `unshare(CLONE_NEWUSER)`)
  starts with a **full** permitted and effective set in the new namespace.
- Its capabilities in the **parent** namespace are unchanged for `unshare()`;
  practically, rule 3 still gives the creator (as the owner) power over the new
  namespace from outside.
- At the next `execve()`, the normal rules of section 3 apply **within the
  namespace**: UID 0 inside keeps the bounding set (root rule); unmapped or
  non-zero UIDs lose everything unless ambient.
- `setns()` into a user namespace gives a full capability set in it, if the caller
  had `CAP_SYS_ADMIN` in that namespace.

## Why this matters for containers

This is the formal basis of the two kinds of "root in a container":

| | Without a user namespace (Docker/Kubernetes default) | With a user namespace (rootless, `userns-remap`, `hostUsers: false`) |
|---|---|---|
| UID of container root in the kernel | 0 | an unprivileged host UID (e.g. 100000) |
| Where its capabilities apply | `init_user_ns` (the host), limited by the bounding set | only the container's user namespace |
| `CAP_DAC_OVERRIDE` on a host file owned by UID 0 in a bind mount | **effective**: can write it | **not effective**: host UID 0 is not mapped |
| Owner permissions on host files owned by UID 0 | **yes**: it is UID 0 | no |
| A leaked host file descriptor or a kernel bug exploited | acts as host root with the container's capabilities | acts as an unprivileged host user |
| Adding `CAP_SYS_ADMIN` | close to full host root | powerful only inside the container's namespaces |

The right column is why user namespaces are considered the strongest standard
container isolation primitive, and the left column explains why the default
capability set of section 5 has to be chosen so carefully.

## Evidence

- Chapter 03 [`lab-07-user-namespace`](../../labs/03-namespaces/lab-07-user-namespace/)
  (Parts C–F) demonstrates rules 1–3.
- This chapter's [`lab-04-root-in-a-container`](../../labs/05-capabilities/lab-04-root-in-a-container/),
  Part E, compares both columns of the table.

## Further Reading

- [`user_namespaces(7)`](https://man7.org/linux/man-pages/man7/user_namespaces.7.html),
  sections "Capabilities", "Effect of capabilities within a user namespace", and
  "Interaction of user namespaces and other types of namespaces" — the three
  rules above, verbatim in more detail.
- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html),
  "Interaction with user namespaces" and "Namespaced file capabilities".
- Kernel source: [`security/commoncap.c`](https://elixir.bootlin.com/linux/v6.12/source/security/commoncap.c),
  `cap_capable()` — the loop that walks from the target user namespace up to the
  caller's, implementing rules 1–3 in about 40 lines.
- Kernel source: [`fs/inode.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/inode.c)
  and [`fs/namei.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namei.c),
  `capable_wrt_inode_uidgid()` — why `CAP_DAC_OVERRIDE` only applies to files whose
  owner is mapped.
