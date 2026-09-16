# Lab 01 — Capability Sets: UID 0 Is Not the Same as Privilege

## Goal

Produce evidence that:

1. the five capability sets are visible in `/proc/<pid>/status` and decodable;
2. the kernel checks **capabilities**, not UID 0: a root process without a
   capability cannot perform the matching operation;
3. removing a capability from the **bounding set** also removes it from every
   program executed afterwards;
4. dropping capabilities does not change file ownership: root without
   `CAP_DAC_OVERRIDE` still reads files it owns.

## Prerequisites

- Linux VM with `sudo`, `libcap2-bin` (`capsh`, `getpcaps`), `python3`.
- Read: [1. From root to capabilities](../../../docs/05-capabilities/01-root-to-capabilities.md)
  and [2. Capability sets](../../../docs/05-capabilities/02-capability-sets.md).

## Background

`capsh --drop=LIST -- -c 'COMMAND'` removes capabilities from the bounding set
(and from P/E/I), then executes `/bin/bash -c COMMAND`. Because the bounding set
limits what can be gained at `execve()`, the new bash, although UID 0, starts
without those capabilities.

## Experiment

### Part A — Read the sets

```bash
grep Cap /proc/self/status
sudo grep Cap /proc/self/status
capsh --decode=$(sudo awk '/CapEff/ {print $2}' /proc/self/status) | tr ',' '\n' | head -n 5
getpcaps $$
sudo capsh --print | head -n 6
```

### Part B — Root without `CAP_DAC_OVERRIDE`

Create a file that only `CAP_DAC_OVERRIDE` can open (mode `000`):

```bash
echo "secret" | sudo tee /tmp/caplab-000.txt >/dev/null
sudo chmod 000 /tmp/caplab-000.txt
sudo cat /tmp/caplab-000.txt
```

**Predict first.** Root drops `cap_dac_override` and `cap_dac_read_search`. Can
it still read the file?

```bash
sudo capsh --drop=cap_dac_override,cap_dac_read_search -- -c '
  id -u
  grep -E "^Cap(Eff|Bnd)" /proc/self/status
  cat /tmp/caplab-000.txt'
```

### Part C — Root without `CAP_CHOWN` and `CAP_NET_BIND_SERVICE`

```bash
sudo touch /tmp/caplab-owned.txt
sudo capsh --drop=cap_chown -- -c 'chown 1000 /tmp/caplab-owned.txt; echo "chown exit code: $?"'
sudo capsh --drop=cap_net_bind_service -- -c '
  python3 -c "import socket; s=socket.socket(); s.bind((\"127.0.0.1\", 80)); print(\"bound to port 80\")"'
sudo python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 80)); print("bound to port 80")'
```

### Part D — The bounding set is inherited and permanent

```bash
sudo capsh --drop=cap_sys_time -- -c '
  echo "bash:  $(grep CapBnd /proc/self/status)"
  bash -c "echo \"child: \$(grep CapBnd /proc/self/status)\""
  sudo date -s "$(date)" >/dev/null; echo "set clock via sudo inside, exit code: $?"'
```

**Predict first.** `sudo` is a setuid-root program. Can running it *inside* the
reduced shell restore `CAP_SYS_TIME`?

### Part E — Ownership still matters

```bash
echo "root-owned, mode 600" | sudo tee /tmp/caplab-600.txt >/dev/null
sudo chmod 600 /tmp/caplab-600.txt
sudo capsh --drop=cap_dac_override,cap_dac_read_search,cap_fowner -- -c 'cat /tmp/caplab-600.txt'
```

### Cleanup

```bash
sudo rm -f /tmp/caplab-000.txt /tmp/caplab-owned.txt /tmp/caplab-600.txt
```

## Expected observations

**Part A.** User: `CapPrm`/`CapEff` all zero, `CapBnd` all ones (e.g.
`000001ffffffffff`). Root: `CapPrm` and `CapEff` equal to `CapBnd`. `capsh
--decode` begins `cap_chown`, `cap_dac_override`, ... . `getpcaps` for your shell
prints `=` (no capabilities).

**Part B.** `sudo cat` prints `secret`. Inside the reduced shell: `id -u` is `0`,
`CapEff` lacks bits 1 and 2 (`...fffffff9`), and `cat` fails with
`Permission denied`.

**Part C.** `chown` fails with `Operation not permitted` (exit code 1). The bind
fails with `PermissionError: [Errno 13] Permission denied`. Normal root binds
successfully.

**Part D.** Both bash and its child show the same reduced `CapBnd` (bit 25
cleared). `sudo date -s` fails: `date: cannot set date: Operation not permitted`,
because the setuid-root `sudo` cannot gain a capability that is not in the
bounding set.

**Part E.** `cat` succeeds: UID 0 owns the file, and the owner bits `rw-` allow
reading without any capability.

## Why this happens

- **B, C.** The relevant kernel paths call `capable(CAP_DAC_OVERRIDE)`,
  `capable(CAP_CHOWN)`, and (for ports below `ip_unprivileged_port_start`)
  `ns_capable(net->user_ns, CAP_NET_BIND_SERVICE)`, not `euid == 0`.
- **D.** At `execve()`, newly permitted capabilities from root status or file
  capabilities are ANDed with the bounding set (section 3). The bounding set is
  copied at `fork()` and cannot be raised.
- **E.** The owner permission check compares UIDs; no capability is involved.

## Connection to containers

- Parts B–D are what `docker run --cap-drop=...` does: the runtime removes
  capabilities from the bounding set (and the other sets) before `execve()`.
- Part D is why a setuid-root binary inside a container image cannot restore
  dropped capabilities.
- Part E is the reason "root in a container" still matters without a user
  namespace: files owned by host UID 0 in a bind mount are writable by the
  container's root through normal owner permissions (Lab 04).

## Questions to think about

1. Which capability would a JVM-based HTTP server need to bind port 443 directly?
   What alternatives avoid needing it at all?
2. Why does dropping `CAP_DAC_OVERRIDE` alone not stop root from reading
   `/etc/shadow` on most systems? (Check its owner and mode.)
3. Why is the bounding set designed to be impossible to raise again?
