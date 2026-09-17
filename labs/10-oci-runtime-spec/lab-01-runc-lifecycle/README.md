# Lab 01 — An OCI Bundle and the runc Lifecycle

## Goal

Produce evidence that:

1. an OCI bundle is just `config.json` + `rootfs/`;
2. `config.json` sections correspond to the Linux mechanisms of Chapters 01–08;
3. `runc create` sets everything up but does **not** run the process yet;
4. `runc start` runs it; `runc state`, `kill`, and `delete` drive the lifecycle;
5. editing `config.json` changes the container's isolation, no code required.

## Prerequisites

- Linux VM with `sudo`, `runc`, `jq`, and the BusyBox rootfs from Chapter 07
  Lab 01.
- Read: [2. config.json](../../../docs/10-oci-runtime-spec/02-runtime-spec-config.md)
  and [3. the lifecycle](../../../docs/10-oci-runtime-spec/03-runtime-lifecycle.md).

## Experiment

### Part A — Build a bundle

```bash
mkdir -p /tmp/bundle/rootfs
cp -a /tmp/rootfs/. /tmp/bundle/rootfs/     # reuse the Chapter 07 rootfs
cd /tmp/bundle
runc spec                                    # writes ./config.json
ls
jq '.process.args, .root, .linux.namespaces' config.json
```

### Part B — Read the config as Linux mechanisms

```bash
jq '.process.capabilities.bounding | length' config.json     # how many caps
jq '.linux.namespaces[].type' config.json                     # which namespaces
jq '.mounts[] | {destination,type}' config.json               # the mount assembly
jq '.linux.maskedPaths, .linux.readonlyPaths' config.json     # Ch. 07 §4
jq '.process.args' config.json
```

Set the command to the shell and give it a terminal:

```bash
jq '.process.args = ["/bin/sh"] | .process.terminal = true' config.json > c.json && mv c.json config.json
```

### Part C — create vs start

Open two terminals. Terminal 1:

```bash
cd /tmp/bundle
sudo runc create demo
sudo runc state demo | jq '{status, pid}'
```

**Predict first.** After `create`, is the shell running? What does `status` say?

```bash
sudo runc list
ps -o pid,comm -p "$(sudo runc state demo | jq .pid)"
```

Now start it (terminal 1 becomes the container shell if terminal=true):

```bash
sudo runc start demo
```

In the container shell:

```sh
hostname; echo $$; ps; grep -E 'CapBnd|NoNewPrivs|Seccomp' /proc/self/status
```

### Part D — state, kill, delete

Terminal 2:

```bash
sudo runc state demo | jq '{status, pid, bundle}'
sudo runc kill demo KILL
sleep 0.5; sudo runc state demo | jq .status
sudo runc delete demo
sudo runc list
```

### Part E — Change isolation by editing config only

```bash
# Drop ALL capabilities and set a tiny pids limit, then run:
jq '.process.capabilities |= map_values([]) |
    .linux.resources.pids.limit = 16' config.json > c.json && mv c.json config.json
sudo runc run demo2 <<'EOF'
EOF
```

(Or run interactively: `sudo runc run demo2` then inside try `chown 0 /etc/hostname`.)

Cleanup: `sudo runc delete --force demo2 2>/dev/null; cd /tmp; rm -rf /tmp/bundle`.

## Expected observations

**Part A.** `config.json` appears next to `rootfs/`. `jq` shows
`args=["sh"]`, `root.path="rootfs"`, and the namespace list.

**Part B.** The default bounding set has ~14 entries; namespaces include `pid`,
`network`, `ipc`, `uts`, `mount` (and possibly `cgroup`); mounts include `/proc`,
`/dev`, `/dev/pts`, `/sys`, `/sys/fs/cgroup`; masked and readonly paths match
Chapter 07 §4.

**Part C.** After `create`, `status` is `created` and the process **exists**
(a `runc:[2:INIT]` process, blocked) but the shell/entrypoint is **not** running.
After `start`, the shell runs; inside, `hostname` is the config's hostname,
`$$` is 1, `ps` shows few processes, and `/proc/self/status` shows the reduced
`CapBnd`, `NoNewPrivs: 1`, `Seccomp: 2`.

**Part D.** `state` shows `running` then, after `kill`, `stopped`. `delete`
removes it from `runc list`.

**Part E.** With all capabilities removed, `chown` inside fails with
`Operation not permitted`; with `pids.limit=16`, a fork bomb stops at 16. You
changed the container's behavior by editing JSON only.

## Why this happens

- `runc create` performs the whole Chapter 09 setup (namespaces, cgroups, rootfs,
  caps, seccomp) and then blocks the init process on the start FIFO; `runc start`
  writes the FIFO so the process `execve`s (Chapter 10 §3).
- Every field you edited maps to a syscall/mechanism from Chapters 01–08
  (Chapter 10 §2).

## Connection to containers

- This is exactly the interface containerd's shim uses to drive runc
  (Chapter 12): `create`, then `start`, then `state`/`kill`/`delete`.
- `minic` (Chapter 09) is a hard-coded `runc run`; this lab shows the standard,
  configurable version.

## Questions to think about

1. Why does the OCI split `create` and `start`? What can a manager do in between?
2. In `config.json`, how would you make the container join an existing network
   namespace instead of creating one? (Chapter 03 §1, Chapter 10 §2.)
3. Which `config.json` fields would you change to reproduce
   `minic run -userns -mem ... -pids ...`?
4. After `runc create` but before `start`, what does `/proc/<pid>/status` of the
   `runc:[2:INIT]` process show for `Seccomp` and `CapBnd`, and why?
