# 5. Two Real Vulnerabilities

Reading runc's most famous CVEs is one of the best ways to consolidate the whole
repository: each one is a failure of a specific mechanism you now understand, and
each fix is a small change in the code paths from §§2–4. Both are **container
escapes** — a process inside a container gaining code execution on the host.

## CVE-2019-5736: overwriting the runc binary via /proc/self/exe

### The mechanism abused

Chapter 01 §4 introduced **magic links**: `/proc/<pid>/exe` refers to the running
executable, and `/proc/self/exe` is the caller's own binary. Chapter 01 §5 showed
that an **open file descriptor** bypasses path-based restrictions.

### The attack

When runc executes into a container (or `docker exec`s), for a moment
`/proc/self/exe` of the container's init points at the **runc binary on the
host** (runc re-execs itself, §1). A malicious image or process could:

1. replace the container's entrypoint with something that opens
   `/proc/self/exe` — which resolves to the host's runc binary — and keeps the
   descriptor;
2. then open that descriptor for writing (`/proc/self/fd/<n>`) after runc has
   dropped into the container, and overwrite the host `runc` binary with attacker
   code;
3. the next time anyone runs `runc`/`docker run` on the host, the attacker's code
   runs **as root on the host**.

This is a combination of two Chapter 01 facts (magic links + descriptors as
capabilities) turned into a host compromise.

### The fix

runc now **makes a memfd copy of itself** and re-execs from that sealed,
read-only in-memory copy, so the container never has a writable path to the host
binary. The relevant code seals `/proc/self/exe` behind a `memfd`/`O_TMPFILE`
copy before the container can touch it. The lesson maps directly to Chapter 01 §5:
never let a container hold a descriptor to a host object you care about.

## CVE-2024-21626: file-descriptor leak into the container ("Leaky Vessels")

### The mechanism abused

Chapter 01 §5 again: file descriptors are **inherited across `execve`** unless
marked close-on-exec, and an inherited descriptor to a host directory bypasses the
mount namespace and `pivot_root` (Chapter 07), because it refers to an
already-open object, not a path.

### The attack

runc (in certain versions) leaked an open file descriptor referring to a **host
directory** (an internal fd left open across the container setup) into the
container process. A crafted image could set its working directory to
`/proc/self/fd/<n>` (the leaked host directory) or otherwise reach through it, so
the container's process started with its cwd **on the host filesystem**, outside
the rootfs — a direct escape and host file access.

### The fix

runc audited and closed (or set close-on-exec on) all internal descriptors before
executing the container process, and added checks that the working directory is
inside the rootfs. Again the lesson is Chapter 01 §5: the rootfs and mount
namespace change how **paths** resolve, but they cannot revoke a descriptor to an
already-open host object; the runtime must close them.

## What these teach

| CVE | Mechanism | Chapter | Fix idea |
|---|---|---|---|
| 2019-5736 | magic link `/proc/self/exe` + writable host binary | 01 §4, §5 | re-exec from a sealed memfd copy |
| 2024-21626 | leaked host directory fd bypasses rootfs | 01 §5, 07 | close/cloexec all internal fds; validate cwd |

Both confirm a claim made throughout: the container boundary is built from
several independent mechanisms over **one shared kernel and one shared process
model**, and a single leaked descriptor or magic link can defeat namespaces,
mounts, capabilities, and seccomp at once. This is also the strongest argument for
**user namespaces** (Chapter 03 §7, Chapter 05 §4): with container root mapped to
an unprivileged host UID, even a successful escape lands without host privilege.

## Why this matters

- These are not exotic; they are the exact Chapter 01 mechanisms, used against the
  runtime. Understanding them is understanding the limits of container isolation.
- When you read runc's `standard_init_linux.go` and `process_linux.go`, you will
  now recognize the fd-closing and the memfd self-copy as defenses, not noise.

## Further Reading

- runc advisory [GHSA-gxmr-w5mj-v8hh (CVE-2019-5736)](https://github.com/opencontainers/runc/security/advisories)
  and the original disclosure by Adam Iwaniuk and Borys Popławski; many detailed
  write-ups exist — read one after this section.
- runc advisory [GHSA-xr7r-f8xq-vfvv (CVE-2024-21626)](https://github.com/opencontainers/runc/security/advisories/GHSA-xr7r-f8xq-vfvv)
  and Snyk's ["Leaky Vessels"](https://snyk.io/blog/leaky-vessels-docker-runc-container-breakout-vulnerabilities/)
  write-up (also cited in Chapter 01 §5).
- Chapters 01 §4–§5, 03 §7, 05 §4, 07 — the mechanisms and defenses involved.
