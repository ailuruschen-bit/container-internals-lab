# 7. The Finished Mini Container

## Running it

```bash
sudo ./minic run -hostname box -mem $((128*1024*1024)) -pids 128 -- /bin/sh
```

Inside, a single command tour of everything the previous sections added:

```sh
hostname                                   # box                      (§2 UTS)
echo $$                                     # 1                        (§2 PID)
ps                                          # only sh + ps             (§3 /proc)
ls /                                        # the rootfs, not the host (§3 pivot_root)
cat /proc/mounts                            # short, container-like    (§3)
ipcs -m 2>/dev/null                         # empty                    (§4 IPC)
cat /proc/self/cgroup                        # minic-<pid>             (§5 cgroup)
grep -E 'CapBnd|NoNewPrivs|Seccomp' /proc/self/status  # reduced/1/2  (§6)
mount -t tmpfs x /mnt 2>&1                    # Operation not permitted (§6)
```

## The rootless variant

```bash
./minic run -userns -hostname box -- /bin/sh
```

`-userns` adds `CLONE_NEWUSER` and the uid/gid maps (Chapter 03 §7). Now:

- no `sudo` is needed;
- inside, `id` shows `uid=0(root)`, but from the host the process is your normal
  user (`ps -o uid`), so an escape lands unprivileged (Chapter 05 §4);
- the namespaces are all owned by the new user namespace, so `sethostname`,
  `pivot_root`, and mounts of `proc`/`tmpfs` succeed without real root
  (Chapter 03 §7, Chapter 05 §4);
- cgroup limits may be skipped unless systemd delegated a subtree (§5 caveat).

This is, at the level of kernel primitives, what a rootless Podman/nerdctl
container is.

## The whole picture, as one clone plus setup

Everything `minic` does reduces to the pattern from Chapter 01 §3, now fully
populated:

```text
parent:
  clone(/proc/self/exe "child",
        CLONE_NEWUTS|CLONE_NEWPID|CLONE_NEWNS|CLONE_NEWIPC[|CLONE_NEWNET][|CLONE_NEWUSER])
  [write uid_map/gid_map]                       (Ch. 03 §7)
  create cgroup, write memory.max/pids.max, add child   (Ch. 04)
  wait

child (PID 1 in the new namespaces):
  sethostname(box)                              (Ch. 03 §2)
  make-rprivate; bind rootfs; mount proc/sys/dev; pivot_root; detach old root  (Ch. 07)
  PR_CAPBSET_DROP(...); PR_SET_NO_NEW_PRIVS; seccomp(filter)   (Ch. 05, 06)
  execve(/bin/sh)                               (Ch. 01 §3)
```

Compare this with the container-start sketch at the end of Chapter 01 §3. You
have now written it.

## minic vs a real runtime

| Concern | minic | runc (Chapter 11) |
|---|---|---|
| Config | flags + env | an OCI `config.json` (Chapter 10) |
| Namespace entry | Go `clone` via SysProcAttr | C `nsexec` bootstrap |
| Setup ordering | best-effort | strict, via a sync pipe |
| Capabilities | drop from bounding set | full OCI five-set application via capset |
| seccomp | hand-built deny list | libseccomp allow-list profile |
| Rootfs | prepared directory | OverlayFS from image layers (Chapter 08/12) |
| Networking | isolated only | delegated to CNI/libnetwork |
| Devices | a few mknod nodes | device cgroup (eBPF) + OCI device list |
| Lifecycle | run and wait | create/start/kill/delete, state, hooks |

Every row on the right is something you now understand well enough to read in
runc's source.

## Further Reading

- Chapter 10 (OCI) turns `minic`'s flags into a standard `config.json`.
- Chapter 11 (runc) is `minic` done properly; the table above is your reading
  map.
- Liz Rice, ["Containers From Scratch"](https://www.youtube.com/watch?v=8fi7uSYlOdc)
  (GOTO 2018) — a similar Go build, useful as a second perspective.
