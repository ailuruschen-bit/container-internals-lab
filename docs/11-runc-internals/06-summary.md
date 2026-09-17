# 6. Chapter Summary: minic, Done Right

## The whole path

`runc run` is `minic` made correct, complete, and secure:

```text
runc run <id>                                                       (Ch. 10 §3)
  CLI → libcontainer.Create/Start → newParentProcess               §1
    re-exec /proc/self/exe init  (like minic)                      §1 (Ch. 09 §1)
      nsexec (C, pre-Go): 3-stage clone/unshare/setns + uid maps   §2 (Ch. 03)
      Go init (standard):
        prepareRootfs: mounts, devices, pivot_root                 §3 (Ch. 02, 07)
        finalizeRootfs: masked/readonly paths, ro rootfs           §3 (Ch. 07 §4)
        network/sysctl/LSM                                          §4 (Ch. 03, 06)
        setupUser + capabilities (five sets)                       §4 (Ch. 01 §7, 05)
        no_new_privs                                                §4 (Ch. 06 §1)
      [parent: cgroup Apply/Set; hooks at sync points]             §4 (Ch. 04, 10 §3)
        wait on exec FIFO  ← runc start                            §4 (Ch. 10 §3)
        seccomp                                                    §4 (Ch. 06)
        execve(container command)                                  §4 (Ch. 01 §3)
```

The differences from `minic` are all about **correctness at the edges**: a C
bootstrap for the multithreading/ordering problems, a sync pipe for precise
ordering and hooks, both cgroup versions and the systemd driver, the full
five-set capability application, a libseccomp allow-list profile, robust
`pivot_root`, the device cgroup as eBPF, and closing every host descriptor
(§5's lesson).

## The sentence to remember

> runc is the create-configure-execute pattern of Chapter 01, applied to an OCI
> bundle, with every ordering rule and every escape vector from Chapters 01–08
> handled explicitly. Reading it is checking production code against mechanisms
> you have already reproduced.

## How to keep reading runc

1. Start at `run.go`/`create.go`, follow into `libcontainer/process_linux.go`
   (`newParentProcess`, `start`).
2. Read `libcontainer/nsenter/README.md`, then skim `nsexec.c` with the stage
   message names in hand (§2).
3. Read `standard_init_linux.go` `Init()` top to bottom; jump into
   `rootfs_linux.go` for each rootfs call (§3).
4. Read the capability and seccomp packages last (§4); they are small.
5. Read one CVE advisory (§5) to see the defenses in context.

## Self-check questions

1. Why does runc use a C bootstrap instead of doing everything in Go? Name three
   specific constraints. (§2)
2. Explain the three stages of `nsexec` and which namespace rule forces each hop.
   (§2, Ch. 03 §3, §7)
3. In `rootfs_linux.go`, which function makes the host root unreachable, and how
   does it differ from `minic`'s approach? (§3, Ch. 07 §3)
4. Why is the cgroup applied by the parent while capabilities are applied by the
   child? (§4)
5. State the exact order of setuid/capset/no_new_privs/seccomp/execve and why each
   must come where it does. (§4, Ch. 05 §3, Ch. 06 §1)
6. Explain CVE-2019-5736 using Chapter 01's magic links and descriptors, and the
   memfd fix. (§5)
7. Explain CVE-2024-21626 using descriptor inheritance, and why user namespaces
   reduce its impact. (§5, Ch. 05 §4)

## Next: Chapter 12 — containerd

runc runs **one** container from a bundle and exits. Something must build bundles
from images, keep containers alive and supervised, manage image pulls and
storage, and expose an API. Chapter 12 covers containerd: the content store,
snapshotters (Chapter 08), images, containers vs tasks, the runtime v2 shim (how
runc is actually invoked and the container kept running after runc exits), and one
traced container-creation path.
