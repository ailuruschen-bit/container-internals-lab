# Lab 02 — File Capabilities: Granting One Privilege to One Program

## Goal

Produce evidence that:

1. a file capability lets an unprivileged user's process perform one privileged
   operation, without setuid;
2. the capability is stored in the `security.capability` extended attribute;
3. the file effective bit decides whether a capability-dumb program can use it;
4. file capabilities are ignored on `nosuid` mounts, under `no_new_privs`, and
   when the bounding set excludes them.

## Prerequisites

- Linux VM with `sudo`, `libcap2-bin`, `attr` (`getfattr`), `util-linux`
  (`setpriv`), `python3`.
- Read: [2. Capability sets and file capabilities](../../../docs/05-capabilities/02-capability-sets.md).

## Setup

Python is a convenient "capability-dumb" program: it never raises capabilities
itself. We use a private copy so the system `python3` is not modified.

```bash
PYBIN=$(readlink -f "$(command -v python3)")
cp "$PYBIN" /tmp/caplab-python3
BIND='import socket; s=socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", 80)); print("bound to port 80")'
cat /proc/sys/net/ipv4/ip_unprivileged_port_start     # expect 1024
```

## Experiment

### Part A — No capability

```bash
/tmp/caplab-python3 -c "$BIND"; echo "exit code: $?"
```

### Part B — `+ep`: permitted and effective

```bash
sudo setcap cap_net_bind_service=+ep /tmp/caplab-python3
getcap /tmp/caplab-python3
getfattr -n security.capability -e hex /tmp/caplab-python3
ls -l /tmp/caplab-python3
/tmp/caplab-python3 -c "$BIND"; echo "exit code: $?"
/tmp/caplab-python3 -c 'print(open("/proc/self/status").read().split("CapInh")[1][:120])'
```

**Predict first.** Does the file have the setuid bit? What will `CapPrm` and
`CapEff` of the Python process be?

### Part C — `+p` only: permitted but not effective

```bash
sudo setcap cap_net_bind_service=+p /tmp/caplab-python3
getcap /tmp/caplab-python3
/tmp/caplab-python3 -c "$BIND"; echo "exit code: $?"
/tmp/caplab-python3 -c 'import re; s=open("/proc/self/status").read(); print(re.findall(r"Cap(Prm|Eff):\s+(\w+)", s))'
sudo setcap cap_net_bind_service=+ep /tmp/caplab-python3
```

### Part D — A `nosuid` mount ignores file capabilities

```bash
mkdir -p /tmp/caplab-nosuid
sudo mount -t tmpfs -o nosuid,mode=1777 caplab /tmp/caplab-nosuid
sudo cp --preserve=xattr /tmp/caplab-python3 /tmp/caplab-nosuid/python3   # tmpfs supports security.* xattrs
getcap /tmp/caplab-nosuid/python3
/tmp/caplab-nosuid/python3 -c "$BIND"; echo "exit code: $?"
sudo umount /tmp/caplab-nosuid
```

### Part E — `no_new_privs` ignores file capabilities

```bash
setpriv --no-new-privs /tmp/caplab-python3 -c "$BIND"; echo "exit code: $?"
```

### Part F — The bounding set masks file capabilities

```bash
sudo capsh --drop=cap_net_bind_service --user="$(id -un)" -- -c "/tmp/caplab-python3 -c '$BIND'; echo \"exit code: \$?\""
```

**Predict first.** The process runs as your user (like Part B), and the file
still has `cap_net_bind_service=ep`. Why could it fail?

### Cleanup

```bash
rm -f /tmp/caplab-python3; rmdir /tmp/caplab-nosuid
```

## Expected observations

**Part A.** `PermissionError: [Errno 13] Permission denied`, exit code 1.

**Part B.** `getcap` prints `/tmp/caplab-python3 cap_net_bind_service=ep`;
`getfattr` shows `security.capability=0x...`; `ls -l` shows **no** `s` bit.
`bound to port 80`. The status excerpt shows `CapPrm` and `CapEff` equal to
`0000000000000400` (bit 10, `CAP_NET_BIND_SERVICE`), with the process still
running as your UID.

**Part C.** `getcap` shows `cap_net_bind_service=p`. The bind fails; the status
shows `CapPrm 0000000000000400` but `CapEff 0000000000000000`: the capability is
available but Python never raises it.

**Part D.** `getcap` still reports the capability on the copy, but the bind fails
with `Permission denied`.

**Part E.** The bind fails with `Permission denied`.

**Part F.** The bind fails: the bounding set of the `capsh` process no longer
contains `cap_net_bind_service`, and file permitted capabilities are ANDed with
it at `execve()`.

## Why this happens

- **B, C.** At `execve()`, `P'(permitted) = F(permitted) & P(bounding)` (plus
  other terms, section 3), and `P'(effective)` is `P'(permitted)` only if the file
  effective bit is set.
- **D.** `MNT_NOSUID` disables both setuid bits and file capabilities in the
  kernel's `bprm` credential calculation.
- **E.** With `no_new_privs`, `execve()` never grants privileges that the process
  did not already have.
- **F.** The bounding set is the upper limit for anything gained at `execve()`.

## Connection to containers

- Images sometimes rely on file capabilities (for example, a binary with
  `cap_net_bind_service=+ep` so that a non-root container user can bind port
  80). Part F explains why that fails if the runtime dropped the capability from
  the bounding set, and Part E why it fails with Kubernetes
  `allowPrivilegeEscalation: false` (which sets `no_new_privs`).
- Part D is one reason container volumes are often mounted `nosuid`.

## Questions to think about

1. Why is `cap_net_bind_service=+ep` on a binary safer than making it
   setuid-root? What could still go wrong?
2. A container runs as UID 1000 with the default Docker capability set and
   `allowPrivilegeEscalation: false`. Its image has a file-capability binary for
   port 80. What happens, and what are two ways to make the service work?
3. Why must copying a file-capability binary preserve extended attributes to
   keep the capability? What does `docker build` need to do to keep them in image
   layers?
