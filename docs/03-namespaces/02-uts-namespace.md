# 2. UTS Namespace

The UTS namespace is the simplest namespace, which makes it the best place to
see the complete namespace API without distractions.

## 1. The global resource before isolation

Every Linux system has a **hostname** and an (obsolete) **NIS domain name**.
They are stored in the kernel and returned by the `uname()` system call:

```c
#include <sys/utsname.h>
struct utsname {
    char sysname[];    /* "Linux" */
    char nodename[];   /* the hostname */
    char release[];    /* kernel release, e.g. "6.8.0-45-generic" */
    char version[];    /* kernel build version */
    char machine[];    /* hardware, e.g. "x86_64" */
    char domainname[]; /* NIS domain name (GNU extension) */
};
int uname(struct utsname *buf);
int sethostname(const char *name, size_t len);
```

Without namespaces there is one `nodename` for the whole machine. A program
that calls `sethostname()` (with `CAP_SYS_ADMIN`) changes it for everyone.

## 2. What the namespace isolates

A UTS namespace holds its own copy of **`nodename` and `domainname`**. It does
**not** isolate `sysname`, `release`, `version`, or `machine`: those describe
the one shared kernel, and they are the same in every namespace. This is a small
but real example of "one kernel": a container can have any hostname, but
`uname -r` always shows the host's kernel version.

## 3. What changes from the process's perspective

| Call | Before (initial UTS ns) | After joining a new UTS ns |
|---|---|---|
| `uname().nodename`, `hostname` | host's name | a copy of the creator's name, then independent |
| `sethostname("web-1")` | changes it for the whole host | changes it only in this namespace |
| `uname().release` | host kernel | **same** host kernel |

A new UTS namespace starts as a **copy** of the creator's values, so right after
creation the hostname looks unchanged. Only after `sethostname()` do the two
diverge.

## 4. Kernel API

The kernel keeps a `struct uts_namespace` containing a `struct new_utsname`.
`nsproxy->uts_ns` points to it. `uname()` and `sethostname()` simply read or
write `current->nsproxy->uts_ns->name`. That is the entire mechanism.

`sethostname()` requires `CAP_SYS_ADMIN` **in the user namespace that owns the
UTS namespace** (section 7). For now, root.

## 5. `clone()`, `unshare()`, `setns()`

All three work for UTS:

```c
clone(child_fn, stack_top, CLONE_NEWUTS | SIGCHLD, arg);  // child in a new UTS ns
unshare(CLONE_NEWUTS);                                     // me, in a new UTS ns
setns(open("/proc/4242/ns/uts", O_RDONLY), CLONE_NEWUTS); // me, in 4242's UTS ns
```

Compare the `clone()` line with Chapter 01 Lab 04: it is the same call with one
extra flag.

## 6. Shell experiment

```bash
sudo unshare --uts bash -c 'hostname container-1; hostname; uname -r'
hostname; uname -r
```

## 7. Minimal C example

`uts_clone.c` in the lab creates a child with `clone(CLONE_NEWUTS)`, sets the
child's hostname, and has the parent print its own hostname afterwards:

```c
static int child_fn(void *arg) {
    sethostname(arg, strlen(arg));            // only affects the new namespace
    struct utsname u; uname(&u);
    printf("child:  nodename=%s release=%s\n", u.nodename, u.release);
    return 0;
}
...
pid_t pid = clone(child_fn, stack + STACK_SIZE, CLONE_NEWUTS | SIGCHLD, "container-1");
waitpid(pid, NULL, 0);
uname(&u);
printf("parent: nodename=%s release=%s\n", u.nodename, u.release);
```

## 8. How container runtimes use it

- Every container normally gets its own UTS namespace, and the runtime calls
  `sethostname()` with the configured hostname (the OCI `hostname` field) in the
  gap before `execve()`.
- Docker's default hostname is the short container ID; Kubernetes sets it to
  the pod name.
- `--uts=host` (Docker) or `hostNetwork: true` (Kubernetes, which also shares
  UTS) skip the new namespace, so the container sees the node's hostname.
- A JVM calling `InetAddress.getLocalHost()` starts from this hostname, and
  then resolves it through `/etc/hosts`, which is why runtimes also bind-mount
  a generated `/etc/hosts` containing the container hostname (Chapter 02 §3).

## Evidence

Lab: [`lab-02-uts-namespace`](../../labs/03-namespaces/lab-02-uts-namespace/)

## Further Reading

- [`uts_namespaces(7)`](https://man7.org/linux/man-pages/man7/uts_namespaces.7.html)
  — two paragraphs; confirms exactly which identifiers are isolated.
- [`uname(2)`](https://man7.org/linux/man-pages/man2/uname.2.html) and
  [`sethostname(2)`](https://man7.org/linux/man-pages/man2/sethostname.2.html)
  — the system calls whose behavior the namespace changes.
- Michael Kerrisk, LWN, ["Namespaces in operation, part 2: the namespaces API"](https://lwn.net/Articles/531381/)
  — uses the UTS namespace to demonstrate `clone()`, `setns()`, and `unshare()`
  with small C programs, very close to this section's approach.
- Kernel source: [`kernel/utsname.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/utsname.c)
  (`copy_utsname()`) and [`kernel/sys.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/sys.c)
  (`SYSCALL_DEFINE2(sethostname, ...)`) — a complete namespace implementation
  small enough to read in ten minutes.
