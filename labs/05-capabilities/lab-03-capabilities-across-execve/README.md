# Lab 03 — Capabilities Across execve: Root Rule, UID Changes, Ambient

## Goal

Produce evidence that:

1. a non-root process with capabilities loses them when it executes an ordinary
   program;
2. root regains every bounding-set capability at each `execve()`;
3. switching all UIDs away from 0 clears capabilities, and `keep-caps`
   preserves only the permitted set;
4. ambient capabilities let a non-root program keep a capability across
   `execve()`;
5. `SECBIT_NOROOT` removes root's special treatment.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`setpriv`), `libcap2-bin` (`capsh`),
  `python3`.
- Read: [3. Capabilities across execve](../../../docs/05-capabilities/03-capabilities-across-execve.md).

## Setup

```bash
U=$(id -u); G=$(id -g)
BIND='import socket; s=socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", 80)); print("bound to port 80")'
```

Every `grep ... /proc/self/status` below is run by a freshly executed program,
so it shows that program's capability sets **after** its `execve()`.

## Experiment

### Part A — Root regains capabilities at every execve

```bash
sudo capsh --caps="cap_net_bind_service+eip" -- -c 'grep -E "^Cap(Prm|Eff)" /proc/self/status'
```

`capsh --caps=` reduces the permitted/effective/inheritable sets of `capsh`
itself to one capability, and then executes bash.

**Predict first.** The shell is executed with only `cap_net_bind_service` in
its permitted set, but it is UID 0. What will its `CapPrm` be?

### Part B — Leaving UID 0

```bash
sudo setpriv --reuid=$U --regid=$G --clear-groups bash -c 'grep -E "^Cap(Prm|Eff)" /proc/self/status'
```

With `--inh-caps` and `--bounding-set` but **without** ambient:

```bash
sudo setpriv --reuid=$U --regid=$G --clear-groups --inh-caps=+net_bind_service \
     python3 -c "$BIND"; echo "exit code: $?"
```

**Predict first.** Python runs as your UID. The thread's inheritable set contains
`net_bind_service`. Can it bind port 80?

### Part C — Ambient capabilities

```bash
sudo setpriv --reuid=$U --regid=$G --clear-groups \
     --inh-caps=+net_bind_service --ambient-caps=+net_bind_service \
     bash -c 'grep -E "^Cap(Prm|Eff|Inh|Amb)" /proc/self/status; python3 -c "$0"' "$BIND"
```

Ambient capabilities also survive a second `execve()`:

```bash
sudo setpriv --reuid=$U --regid=$G --clear-groups \
     --inh-caps=+net_bind_service --ambient-caps=+net_bind_service \
     bash -c 'bash -c "sh -c \"grep CapAmb /proc/self/status\""'
```

### Part D — Ambient is cleared by a privileged file

```bash
cp "$(readlink -f "$(command -v python3)")" /tmp/caplab-py-fcap
sudo setcap cap_sys_nice=+ep /tmp/caplab-py-fcap
sudo setpriv --reuid=$U --regid=$G --clear-groups \
     --inh-caps=+net_bind_service --ambient-caps=+net_bind_service \
     /tmp/caplab-py-fcap -c "$BIND"; echo "exit code: $?"
rm -f /tmp/caplab-py-fcap
```

**Predict first.** The file grants `cap_sys_nice`. Will the ambient
`net_bind_service` survive?

### Part E — Securebits: root without the root rule

```bash
sudo capsh --secbits=0x1 --caps="" -- -c 'id -u; grep -E "^Cap(Prm|Eff)" /proc/self/status; cat /etc/shadow >/dev/null; echo "read shadow exit code: $?"'
```

`--secbits=0x1` sets `SECBIT_NOROOT`.

## Expected observations

**Part A.** `CapPrm` and `CapEff` are the **full** set (for example
`000001ffffffffff`): the root rule gave the new bash everything in the bounding
set, regardless of the reduced permitted set of `capsh`.

**Part B.** After `setpriv --reuid`, `CapPrm` and `CapEff` are all zeros. With
only the inheritable bit, the bind fails (`Permission denied`), because Python's
file inheritable set is empty (`P(inheritable) & F(inheritable) = 0`).

**Part C.** The bash shows `CapInh`, `CapAmb`, `CapPrm`, and `CapEff` all equal
to `0000000000000400`, and Python prints `bound to port 80` while running as your
UID. The third nested shell still shows `CapAmb: 0000000000000400`.

**Part D.** The bind fails: executing a file with file capabilities cleared the
ambient set; the process received only `cap_sys_nice` from the file.

**Part E.** `id -u` prints `0`, but `CapPrm` and `CapEff` are zero, and reading
`/etc/shadow` succeeds or fails depending on its owner and mode (on
Debian/Ubuntu it is `root:shadow 640`, so UID 0 still reads it as **owner**).
Try `cat /tmp/caplab-000.txt` from Lab 01 instead to see a failure. UID 0 without
the root rule is an ordinary user who happens to own root's files.

## Why this happens

- **A.** Real/effective UID 0 makes the kernel treat `F(permitted)` as all
  capabilities; ANDed with the full bounding set, that is everything.
- **B.** When all UIDs leave 0, `cap_emulate_setxuid()` clears permitted and
  effective. Inheritable alone is not enough for an ordinary file.
- **C.** `P'(permitted) = ... | P'(ambient)` and `P'(effective) = P'(ambient)` for
  files without the effective bit.
- **D.** `P'(ambient) = 0` if the file is privileged.
- **E.** With `SECBIT_NOROOT`, `cap_bprm_creds_from_file()` skips the root
  special case.

## Connection to containers

- Part A is why `docker exec` into a root container gives a shell with the full
  **bounding** set of the container, whatever the main process did to its own
  permitted set.
- Parts B and C explain the non-root container behavior described in section 3:
  capabilities added with `--cap-add` do not reach a non-root process unless the
  runtime sets them as ambient.
- Part D is why mixing file-capability binaries and ambient capabilities in one
  image behaves unexpectedly.

## Questions to think about

1. Write the sequence of `prctl`/`capset`/`setresuid` calls a runtime must make to
   start a non-root container process that can bind port 80 without file
   capabilities.
2. Why are ambient capabilities cleared when executing a setuid or file-capability
   program?
3. What would a container runtime gain by setting `SECBIT_NOROOT` for root
   containers? Why do you think Docker does not do it by default?
