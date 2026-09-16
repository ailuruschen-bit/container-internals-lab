# 2. Capability Sets and File Capabilities

## The problem: "does the process have CAP_X?" is not one question

If a process simply had one set of capabilities, several needs could not be
expressed:

- temporarily **disabling** a capability while parsing untrusted input, and
  enabling it again later;
- deciding which capabilities may be **passed to programs** the process
  executes;
- setting an upper **bound** that no later action, including executing a
  setuid-root program, can exceed;
- granting a capability to a **program file** instead of a user.

Linux therefore keeps **five capability sets per thread** and **three capability
fields per executable file**.

## The five thread capability sets

All five are bit masks stored in the thread's credentials (`struct cred`,
Chapter 01 §7) and shown in `/proc/<pid>/status` as hexadecimal numbers.

| Set | `/proc` field | Meaning |
|---|---|---|
| **Effective** (E) | `CapEff` | the capabilities the kernel **checks** right now |
| **Permitted** (P) | `CapPrm` | the capabilities the thread **may** put into its effective set; a limiting superset of E |
| **Inheritable** (I) | `CapInh` | capabilities preserved across `execve()` **if** the executed file also lists them as inheritable |
| **Bounding** (B) | `CapBnd` | an upper limit on capabilities that can be gained at `execve()`; can only be reduced |
| **Ambient** (A) | `CapAmb` | capabilities preserved across `execve()` of **ordinary** (non-capability, non-setuid) programs; Linux 4.3+ |

Their relationships:

```text
Bounding  ⊇  what can ever be gained through file capabilities / root at execve
Permitted ⊇  Effective
Permitted ∩ Inheritable ⊇ Ambient      (a capability must be in both P and I to be ambient)
```

Rules for changing them:

- A thread can **drop** capabilities from P, E, I at any time (`capset()`).
- It can **add** to E only what is in P.
- It can add to I only what is in P, unless it has `CAP_SETPCAP`.
- It can **never add to P** except through `execve()`.
- It can remove from the bounding set with `prctl(PR_CAPBSET_DROP, cap)`
  (requires `CAP_SETPCAP`); nothing can add to it again. It is inherited by all
  children.
- Ambient capabilities are raised with
  `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap)` and are cleared automatically
  when a capability leaves P or I.

Capability sets are **per thread**, although in practice all threads of a
process normally have the same values. `fork()` copies all five sets.

### Reading and decoding

```bash
$ grep Cap /proc/self/status
CapInh: 0000000000000000
CapPrm: 0000000000000000
CapEff: 0000000000000000
CapBnd: 000001ffffffffff
CapAmb: 0000000000000000

$ capsh --decode=000001ffffffffff
0x000001ffffffffff=cap_chown,cap_dac_override,cap_dac_read_search,...,cap_checkpoint_restore
```

Bit *n* corresponds to capability number *n* (`CAP_CHOWN` is 0, `CAP_SYS_ADMIN`
is 21, `CAP_CHECKPOINT_RESTORE` is 40). `getpcaps <pid>` prints the sets of a
running process in text form.

A normal user's shell: P=E=I=A=0, bounding = all. A root shell: P=E=all, I=A=0,
bounding = all.

### The system calls

```c
#include <sys/capability.h>  /* libcap wrapper; the raw syscalls are capget(2)/capset(2) */
int capget(cap_user_header_t hdrp, cap_user_data_t datap);   /* read E, P, I */
int capset(cap_user_header_t hdrp, const cap_user_data_t datap); /* change E, P, I */

#include <sys/prctl.h>
prctl(PR_CAPBSET_READ, cap);                      /* bounding set membership */
prctl(PR_CAPBSET_DROP, cap);                      /* remove from bounding set */
prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0);
prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);
prctl(PR_SET_KEEPCAPS, 1);                        /* keep P when leaving UID 0 (section 3) */
```

Go code in container runtimes uses `golang.org/x/sys/unix` for `Prctl`, and the
`github.com/moby/sys/capability` package (a maintained fork of
`syndtr/gocapability`) for `capget`/`capset`.

## File capabilities

A program file can carry capabilities in an **extended attribute** named
`security.capability`. This is the fine-grained alternative to the setuid bit.

| File field | Meaning |
|---|---|
| **Permitted** (F(P)) | capabilities granted to the process at `execve()`, subject to the bounding set |
| **Inheritable** (F(I)) | capabilities allowed to pass from the thread's inheritable set |
| **Effective** (F(E)) | a single **bit**, not a set: if set, the newly permitted capabilities also become effective immediately |

```bash
$ sudo setcap cap_net_bind_service=+ep /usr/local/bin/myserver
$ getcap /usr/local/bin/myserver
/usr/local/bin/myserver cap_net_bind_service=ep
$ getfattr -n security.capability -e hex /usr/local/bin/myserver
security.capability=0x...        (a binary structure: version, permitted, inheritable, effective bit)
```

The `+ep` syntax means "add to permitted and set the effective bit". A program
that is **capability-aware** (it raises its own effective capabilities when
needed) is installed with `+p` only; a **capability-dumb** program that expects
privilege to be present from the start needs `+ep`.

Distributions use this, for example, to give `ping` `cap_net_raw` instead of
making it setuid-root (many now avoid even that by using unprivileged ICMP
sockets).

File capabilities are **ignored** when:

- the file is on a filesystem mounted `nosuid` (Chapter 02 §2);
- the process has `no_new_privs` set (Chapter 06);
- the file is executed by a process whose bounding set does not contain them
  (they are masked).

Setting file capabilities requires `CAP_SETFCAP`. Since Linux 4.14, a file
capability can also record the **root UID of a user namespace** (version 3
capability xattrs), so capabilities set inside a user namespace are only honored
in that namespace.

## Why this matters for containers

- The OCI runtime configuration specifies capability sets explicitly:
  `process.capabilities.bounding`, `.effective`, `.inheritable`, `.permitted`,
  `.ambient` (Chapter 10). A runtime must apply them in a careful order, which
  section 3 explains.
- Docker and Kubernetes set bounding, permitted, and effective to the same list.
  Reducing the **bounding** set is what guarantees that even a setuid-root or
  file-capability binary inside the image cannot regain dropped capabilities.
- Container images can contain files with `security.capability` attributes;
  whether they work depends on the bounding set, `nosuid`, and `no_new_privs`.

## Evidence

- Lab: [`lab-01-capability-sets`](../../labs/05-capabilities/lab-01-capability-sets/)
- Lab: [`lab-02-file-capabilities`](../../labs/05-capabilities/lab-02-file-capabilities/)

## Further Reading

- [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html),
  sections "Thread capability sets", "File capabilities", and "Capability
  bounding set".
- [`capsh(1)`](https://man7.org/linux/man-pages/man1/capsh.1.html),
  [`setcap(8)`](https://man7.org/linux/man-pages/man8/setcap.8.html),
  [`getcap(8)`](https://man7.org/linux/man-pages/man8/getcap.8.html),
  [`cap_from_text(3)`](https://man7.org/linux/man-pages/man3/cap_from_text.3.html)
  — tools and the `=ep` text syntax.
- [`capget(2)`](https://man7.org/linux/man-pages/man2/capget.2.html) and
  [`prctl(2)`](https://man7.org/linux/man-pages/man2/prctl.2.html) — the raw
  interfaces a runtime calls.
- [`moby/sys/capability`](https://github.com/moby/sys/tree/main/capability) —
  the Go library used by runc to read and set capability sets.
