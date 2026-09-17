# 4. Finalizing: cgroups, Capabilities, seccomp, execve

## The end of linuxStandardInit.Init()

After the rootfs is ready (§3), `Init()` performs the privilege reduction and the
final exec. The order matters and matches Chapter 05 §3 and Chapter 06 §1 exactly
— reduce privilege **last**, seccomp after `no_new_privs`. Simplified:

```text
Init():                                       (standard_init_linux.go)
    ... rootfs (§3) ...
    setupNetwork / setupRoutes                 configure lo etc. inside the net ns (Ch. 03 §6)
    setSysctls                                 namespaced sysctls (Ch. 02 §5, Ch. 03)
    apparmor.ApplyProfile / label setup        LSM (Ch. 06 §4 note)
    finalizeNamespace(config):
        setupUser(config)                      setgroups, setresgid, setresuid (Ch. 01 §7)
        capabilities: apply the five sets      capset + PR_CAP_AMBIENT (Ch. 05 §2-3)
        set the process's no_new_privs?         (see below)
    prctl(PR_SET_NO_NEW_PRIVS) if configured   (Ch. 06 §1)
    → signal parent (procReady already done); wait on the start FIFO
    seccomp.InitSeccomp(config.Seccomp)        install the filter (Ch. 06)
    system.Execv(args[0], args, env)           execve — the container starts (Ch. 01 §3)
```

## cgroups: applied by the parent

Unlike the above (which run in the child), the **cgroup** is created and
configured by the **parent** process, through the cgroup manager, because it
manipulates `/sys/fs/cgroup` on the host. In `process_linux.go` the parent calls,
around the child's startup:

```text
manager.Apply(pid)     create the cgroup and write pid to cgroup.procs   (Ch. 04 §2)
manager.Set(resources) write memory.max, cpu.max, pids.max, io.max, ...   (Ch. 04 §3-5)
```

The manager comes from the [`opencontainers/cgroups`](https://github.com/opencontainers/cgroups)
module (formerly `libcontainer/cgroups` inside runc). It has a **v1** (`fs`) and a
**v2** (`fs2`) implementation, and a **systemd** driver variant (Chapter 04 §2)
that asks systemd to create a transient scope instead of writing cgroupfs
directly. This is exactly `minic`'s `cgroups.go`, generalized to both cgroup
versions and both drivers, and done with correct ordering via the sync pipe.

## Capabilities: the full five-set application

`minic` only dropped from the bounding set (Chapter 09 §6). runc applies **all
five** OCI capability sets (`bounding`, `effective`, `permitted`, `inheritable`,
`ambient`, Chapter 05 §2) using the
[`libcontainer/capabilities`](https://github.com/opencontainers/runc/tree/main/libcontainer/capabilities)
package (backed by `moby/sys/capability`). The sequence is the one from Chapter
05 §3:

```text
drop bounding-set entries not in config.bounding      PR_CAPBSET_DROP
PR_SET_KEEPCAPS(1); setgroups/setresgid/setresuid     keep perms across the UID change (Ch. 05 §3)
capset(perm/eff/inh = config sets)
PR_CAP_AMBIENT_RAISE for each config.ambient           so non-root keeps them (Ch. 05 §3)
```

This is why the ordering rules you learned in Chapter 05 §3 are not academic:
runc must do exactly this, or a non-root container would silently lose (or a root
container silently keep) capabilities.

## seccomp: the last step before exec

`seccomp.InitSeccomp` (in
[`libcontainer/seccomp`](https://github.com/opencontainers/runc/tree/main/libcontainer/seccomp))
compiles the OCI `linux.seccomp` profile (default action, architectures,
per-syscall rules, argument matches — Chapter 06 §2–3) into a filter using
**libseccomp** (via `libseccomp-golang`) and installs it. It runs **after**
`no_new_privs` is set and **immediately before** `execve`, so the filter covers
the container command but not the setup that needed blocked syscalls (Chapter 06
§1). `minic`'s `seccomp.go` is the hand-built, deny-list toy version of this.

## The start FIFO: created vs running

The child, after all setup, **blocks reading an exec FIFO**. The container is now
in the OCI `created` state (Chapter 10 §3). When `runc start` runs, it writes the
FIFO; the read unblocks and the child proceeds to `execve`, entering `running`.
This is the concrete implementation of the created/running split from Chapter 10
§3 and the "sync pipe" idea from Chapter 09 §5.

## Putting the whole child in order

```text
nsexec (C): namespaces + uid/gid maps           §2   (Ch. 03)
Go init:  rootfs + mounts + pivot_root          §3   (Ch. 02, 07)
          network/sysctl/LSM                          (Ch. 03, 06)
          setupUser + capabilities               §4   (Ch. 01 §7, Ch. 05)
          no_new_privs                                 (Ch. 06 §1)
          [parent: cgroup Apply/Set]                   (Ch. 04)
          wait on exec FIFO  ← runc start writes it    (Ch. 10 §3)
          seccomp                                      (Ch. 06)
          execve(container command)                    (Ch. 01 §3)
```

Compare with `minic`'s `child()` and the Chapter 09 §7 diagram: runc is the same
sequence, made correct, complete, and ordered.

## Why this matters

- This section is the payoff of Chapters 01, 04, 05, 06: the exact order runc uses
  is the order those chapters said was required. Seeing production code obey the
  rules confirms your model.
- It also shows the boundaries of the low-level runtime: it sets up networking
  interfaces *given* a namespace and hooks, applies cgroups and security, and
  execs — but it does not manage images, lifecycle beyond one container, or
  networking policy. That is Chapters 12–13.

## Evidence

Lab: [`lab-01-trace-runc`](../../labs/11-runc-internals/lab-01-trace-runc/)

## Further Reading

- [`libcontainer/standard_init_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/standard_init_linux.go)
  — `Init()` and `finalizeNamespace`.
- [`libcontainer/process_linux.go`](https://github.com/opencontainers/runc/blob/main/libcontainer/process_linux.go)
  — where the parent calls `manager.Apply`/`Set` and drives the sync/FIFO.
- [`opencontainers/cgroups`](https://github.com/opencontainers/cgroups) (fs, fs2,
  systemd) — Chapter 04's manager.
- [`libcontainer/capabilities`](https://github.com/opencontainers/runc/tree/main/libcontainer/capabilities)
  and [`libcontainer/seccomp`](https://github.com/opencontainers/runc/tree/main/libcontainer/seccomp)
  — Chapters 05 and 06 in code.
