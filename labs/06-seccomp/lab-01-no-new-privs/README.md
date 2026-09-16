# Lab 01 — no_new_privs Disarms setuid and File Capabilities

## Goal

Produce evidence that:

1. `no_new_privs` is visible in `/proc/<pid>/status` and is inherited;
2. with it set, a setuid-root program runs but does **not** gain UID 0;
3. with it set, a file-capability program runs but does **not** gain the
   capability;
4. the flag cannot be unset.

## Prerequisites

- Linux VM with `sudo`, `util-linux` (`setpriv`), `libcap2-bin`, `gcc`.
- Read: [1. no_new_privs](../../../docs/06-seccomp/01-no-new-privs.md).

## Setup

A tiny reporter that prints its effective UID and effective capabilities:

A setuid-root copy of `id` reports whether the setuid bit took effect:

```bash
sudo cp "$(command -v id)" /tmp/nnp-id
sudo chmod u+s /tmp/nnp-id                    # setuid-root copy of id
ls -l /tmp/nnp-id
```

## Experiment

### Part A — The flag and its inheritance

```bash
grep NoNewPrivs /proc/self/status
setpriv --no-new-privs bash -c 'grep NoNewPrivs /proc/self/status; bash -c "grep NoNewPrivs /proc/self/status"'
```

**Predict first.** Will the inner `bash` also show `NoNewPrivs: 1`?

### Part B — setuid neutralized

```bash
/tmp/nnp-id -u
setpriv --no-new-privs /tmp/nnp-id -u
```

**Predict first.** The program is setuid-root. What effective UID does each run
report?

### Part C — file capabilities neutralized

```bash
PYBIN=$(readlink -f "$(command -v python3)")
cp "$PYBIN" /tmp/nnp-py
sudo setcap cap_net_bind_service=+ep /tmp/nnp-py
BIND='import socket; s=socket.socket(); s.bind(("127.0.0.1", 80)); print("bound 80")'
/tmp/nnp-py -c "$BIND"; echo "normal exit: $?"
setpriv --no-new-privs /tmp/nnp-py -c "$BIND"; echo "nnp exit: $?"
```

### Part D — One-way

```bash
setpriv --no-new-privs bash -c '
  grep NoNewPrivs /proc/self/status
  python3 -c "import ctypes,os; libc=ctypes.CDLL(None,use_errno=True); print(\"prctl unset returned\", libc.prctl(38,0,0,0,0), \"errno\", ctypes.get_errno())"
  grep NoNewPrivs /proc/self/status'
```

`prctl(38, ...)` is `PR_SET_NO_NEW_PRIVS`; setting it to `0` is the attempt to
unset it.

### Cleanup

```bash
sudo rm -f /tmp/nnp-id /tmp/nnp-py
```

## Expected observations

**Part A.** Your shell: `NoNewPrivs: 0`. Under `setpriv`, both the shell and its
child show `NoNewPrivs: 1`.

**Part B.** `/tmp/nnp-id -u` prints `0` (setuid worked). Under `--no-new-privs`,
it prints your normal UID: the setuid bit was ignored.

**Part C.** Normal run: `bound 80`. Under `--no-new-privs`: the bind fails with
`PermissionError`, exit code non-zero: the file capability was ignored.

**Part D.** `NoNewPrivs: 1` before and after. The `prctl` to unset it returns
`0` (success) but has **no effect** on the flag when the value is 0 — actually
the kernel accepts only setting it to 1; setting it to 0 leaves it at 1. Either
way, the second `grep` still shows `NoNewPrivs: 1`. (Attempting to lower it is a
no-op, not an error.)

## Why this happens

- `no_new_privs` sets a bit in `task_struct` that `execve()`'s credential
  calculation checks: with it set, `cap_bprm_creds_from_file()` skips setuid and
  file-capability privilege gain.
- The bit is copied at `fork()` and never cleared, by design.

## Connection to containers

- This is Kubernetes `allowPrivilegeEscalation: false` and Docker
  `--security-opt no-new-privileges`.
- It is also the flag that lets an **unprivileged** process install a seccomp
  filter (Lab 02), which is why runtimes set it before seccomp.
- It can break images whose entrypoint relies on a setuid helper; recognizing
  `NoNewPrivs: 1` in `/proc` is the fast diagnosis.

## Questions to think about

1. Why does allowing an unprivileged process to install a seccomp filter require
   `no_new_privs`? What attack would be possible otherwise?
2. An image's entrypoint uses `sudo` to drop from root to an app user. What
   happens under `allowPrivilegeEscalation: false`, and what is a better design?
3. Why must `no_new_privs` be impossible to unset for it to be a useful security
   guarantee?
