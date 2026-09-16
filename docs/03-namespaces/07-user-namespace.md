# 7. User Namespace

The user namespace is the most subtle namespace, because it does not isolate a
*view* of a resource like the others. It changes **what identity and privilege
mean**. This section introduces the mechanism. Chapter 05 builds on it to
explain capabilities in full.

## 1. The global resource before isolation

Chapter 01 §7 established:

- the kernel identifies users by numeric UIDs and GIDs;
- UID 0 normally holds every capability;
- a process's credentials, including capabilities, are stored in `struct cred`.

Without user namespaces, these numbers and privileges are **global**:

- UID 0 in any process is the same all-powerful UID 0;
- a program that needs to be "root" for a harmless local operation (setting its
  own hostname in its own UTS namespace, mounting a tmpfs in its own mount
  namespace) must be *real* root, with power over the whole machine;
- therefore creating the other namespace types must be restricted to real
  root (`CAP_SYS_ADMIN`), because a new namespace combined with setuid programs
  could otherwise be used to confuse privileged software.

## 2. What the namespace isolates

A user namespace provides:

1. **A mapping of user and group IDs.** A range of IDs inside the namespace maps
   to a range of IDs in the parent namespace. For example, UID 0 inside maps to
   UID 1000 outside.
2. **A scope for capabilities.** A process can hold capabilities *in* a user
   namespace. Those capabilities apply only to resources **owned by** that
   namespace.
3. **Ownership of other namespaces.** Every non-user namespace is owned by the
   user namespace that was active when it was created.

User namespaces are **hierarchical**: every user namespace except the initial
one has a parent, up to 32 levels.

## 3. What changes from the process's perspective

### ID mappings

A mapping is written to `/proc/<pid>/uid_map` and `/proc/<pid>/gid_map` as
lines of three numbers:

```text
ID-inside-ns   ID-outside-ns   length
0              1000            1          ← UID 0 inside is UID 1000 in the parent
```

With this mapping:

| Situation | What happens |
|---|---|
| `getuid()` inside | returns `0` |
| Creating a file | the inode stores **1000**, the parent's (in fact the initial namespace's) value |
| `stat` of a file owned by 1000, viewed inside | shows owner `0` |
| `stat` of a file owned by any unmapped UID (for example real root, 0), viewed inside | shows the **overflow UID** `65534` (`nobody`) |
| Permission check when opening a file | done with the **outside** UID 1000; being "root inside" does not help with host files |
| Before any mapping is written | `getuid()` returns 65534 |

The kernel stores only initial-namespace IDs in its credentials and inodes
(`kuid_t`, `kgid_t`). The mapping is applied when IDs cross the user/kernel
boundary: when a syscall receives or returns a UID.

### Capabilities inside a user namespace

When a process creates a user namespace (with `clone()` or `unshare()`), it
gets a **full set of capabilities in the new namespace**. This sounds alarming,
but consider what those capabilities can be used for.

The kernel checks a capability *against a namespace*: "does the caller have
`CAP_SYS_ADMIN` **in the user namespace that owns this resource**?"

```text
initial user namespace (owns: host UTS, host network, host mounts, all devices, the kernel)
│    real root has capabilities here
│
└── user namespace U  (created by UID 1000; UID 0 inside ↦ 1000 outside)
     │    a process in U has full capabilities in U
     │
     ├── UTS namespace   created by a process in U   → owned by U → sethostname() allowed
     ├── network ns      created by a process in U   → owned by U → configure its interfaces allowed
     └── mount namespace created by a process in U   → owned by U → mount tmpfs/proc/bind allowed
```

The same process, asking to act on the **host's** resources (owned by the
initial user namespace), has **no** capabilities there:

- `sethostname()` in the host's UTS namespace fails;
- reading `/etc/shadow` fails (file permission check with UID 1000);
- loading kernel modules, changing the system clock, or opening raw block
  devices fails, because those are checked in the initial user namespace;
- mounting most disk filesystems fails, because only filesystems explicitly
  marked safe (`tmpfs`, `proc`, `sysfs`, `overlay`, `fuse`, bind mounts, and a
  few others) can be mounted in a non-initial user namespace.

Also, a process in the **parent** namespace whose effective UID equals the
owner of a child user namespace has all capabilities in that child. This is how
an unprivileged user can write the ID maps of a namespace they created.

### Capabilities across `execve()`

Capabilities are recalculated at `execve()` (Chapter 05 explains the formula).
The practical rule for now: if the process's UID **inside** the namespace is 0
at `execve()`, it keeps the full capability set; otherwise it loses them. That
is why tools map UID 0 **before** executing the target program.

## 4. Kernel API

```c
clone(fn, stack, CLONE_NEWUSER | SIGCHLD, arg);
unshare(CLONE_NEWUSER);     // the caller must be single-threaded
setns(fd, CLONE_NEWUSER);   // requires CAP_SYS_ADMIN in the target; caller must be single-threaded
```

**No privilege is needed** to create a user namespace (subject to distribution
policy, see below).

### Rules for writing `uid_map` and `gid_map`

1. Each file can be written **only once**.
2. The writer must have `CAP_SETUID` (`CAP_SETGID` for `gid_map`) in the target
   user namespace, and be in that namespace or its parent.
3. An **unprivileged** writer (without `CAP_SETUID` in the *parent* namespace)
   may write only a single line mapping **its own effective UID**.
4. For `gid_map`, an unprivileged writer must first write `deny` to
   `/proc/<pid>/setgroups`. Otherwise a process could drop a supplementary group
   with `setgroups()` and bypass permissions like `rwx---r-x` that deny access
   to a group.
5. A **privileged** writer can map ranges, but only to IDs that are mapped in
   the parent.

Rule 3 is why a normal user gets exactly one ID inside. To map a *range*
(for example 65536 IDs, so that a container image with users 0, 33, 999... works),
rootless container tools use the setuid-root helpers **`newuidmap`** and
**`newgidmap`**, which consult `/etc/subuid` and `/etc/subgid`:

```text
/etc/subuid:   alice:100000:65536    → alice may map outside IDs 100000–165535
uid_map:       0  1000    1          → root inside  = alice
               1  100000  65536      → 1..65536 inside = 100000..165535 outside
```

### The order of creation

When `clone()` or `unshare()` receives `CLONE_NEWUSER` together with other
`CLONE_NEW*` flags, the kernel **creates the user namespace first**, so the
other new namespaces are owned by it. This is what allows an unprivileged user
to run:

```bash
unshare --user --map-root-user --uts --mount --net bash
```

and set a hostname, mount tmpfs, and configure `lo` inside.

## 5. Security and distribution policy

User namespaces make large parts of the kernel reachable by unprivileged users:
code that was previously only callable with real `CAP_NET_ADMIN` or
`CAP_SYS_ADMIN` (netfilter, filesystem mounting, and more) can now be reached
with namespace-scoped capabilities. Several privilege-escalation
vulnerabilities have been exploited this way. As a result:

- the sysctl `user.max_user_namespaces` limits how many can exist (0 disables
  them);
- Debian historically used `kernel.unprivileged_userns_clone`;
- Ubuntu 23.10 and later restrict unprivileged user namespaces through AppArmor
  (`kernel.apparmor_restrict_unprivileged_userns=1`): creation succeeds, but the
  process gets no capabilities inside unless an AppArmor profile allows it.

The trade-off is real: user namespaces reduce the privilege needed *to run
containers*, while increasing the kernel attack surface *available to
unprivileged users*.

## 6. How container runtimes use it

- **Rootless containers** (Podman, rootless Docker, rootless containerd/nerdctl)
  run the entire runtime as a normal user inside a user namespace, with
  `newuidmap`/`newgidmap` mapping a subordinate ID range. Root inside the
  container is an unprivileged user on the host.
- **Root-run containers with user namespaces**: Docker's `userns-remap` and
  Kubernetes pods with `hostUsers: false` map container root to a high,
  unprivileged host UID range, so a container escape lands as an unprivileged
  user.
- **Default Docker and Kubernetes containers do *not* use a user namespace.**
  Root inside such a container **is** host UID 0; the restrictions come from
  capability dropping, seccomp, and LSMs (Chapters 05–06). This is the most
  important fact for understanding container security today.
- **File ownership** is the main practical difficulty: files in a volume are
  owned by host IDs, which appear as `65534` inside if unmapped. **Idmapped
  mounts** (Linux 5.12+, `mount_setattr(MOUNT_ATTR_IDMAP)`) solve this by
  applying an ID mapping to a mount, and are used by modern runtimes.
- Order of setup in a runtime: create the user namespace first, have the
  **parent** write the ID maps (via a synchronization pipe), then create or
  enter other namespaces and perform privileged setup inside. runc's `nsexec`
  C code performs exactly this dance (Chapter 11).

## Evidence

Lab: [`lab-07-user-namespace`](../../labs/03-namespaces/lab-07-user-namespace/)

## Further Reading

- [`user_namespaces(7)`](https://man7.org/linux/man-pages/man7/user_namespaces.7.html)
  — long but essential: capabilities in namespaces, ownership, the exact
  `uid_map` writing rules, `setgroups`, and interaction with `execve()`. Read
  it twice: now, and again with Chapter 05.
- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html),
  section "Interaction with user namespaces" — the rule for how a capability is
  checked against a namespace.
- [`newuidmap(1)`](https://man7.org/linux/man-pages/man1/newuidmap.1.html) and
  [`subuid(5)`](https://man7.org/linux/man-pages/man5/subuid.5.html) — the
  helpers rootless containers depend on.
- Michael Kerrisk, LWN, ["Namespaces in operation, part 5: user namespaces"](https://lwn.net/Articles/532593/)
  and ["part 6: more on user namespaces"](https://lwn.net/Articles/540087/) —
  experiments with maps and capabilities.
- Kubernetes documentation: [User Namespaces](https://kubernetes.io/docs/concepts/workloads/pods/user-namespaces/)
  — how `hostUsers: false` uses mappings and idmapped mounts in production.
- Rootless Containers project: [rootlesscontaine.rs](https://rootlesscontaine.rs/)
  — a concise explanation of how rootless runtimes combine user namespaces,
  `newuidmap`, and networking helpers.
- Kernel source: [`kernel/user_namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/user_namespace.c),
  `map_write()` and `new_idmap_permitted()` — the writing rules implemented in C.
