# References — Chapter 03: Namespaces

Version note: kernel source links are pinned to Linux **v6.12**; Go source
links point to the `master` branch of `golang/go`, and the functions named
(`forkAndExecInChild1`) have existed with these names since roughly Go 1.19.

## Linux man-pages (primary)

| Page | Why read it | Used in |
|---|---|---|
| [`namespaces(7)`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | Overview, `/proc/<pid>/ns`, lifetime, limits. Read fully. | §1 |
| [`clone(2)`](https://man7.org/linux/man-pages/man2/clone.2.html), [`unshare(2)`](https://man7.org/linux/man-pages/man2/unshare.2.html), [`setns(2)`](https://man7.org/linux/man-pages/man2/setns.2.html) | The three APIs and their per-type restrictions (single-threaded callers, `CLONE_FS`, pidfds). | §1–8 |
| [`ioctl_ns(2)`](https://man7.org/linux/man-pages/man2/ioctl_ns.2.html) | Discovering parents and owning user namespaces. | §1 |
| [`uts_namespaces(7)`](https://man7.org/linux/man-pages/man7/uts_namespaces.7.html) | What UTS isolates. | §2 |
| [`pid_namespaces(7)`](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html) | Init semantics, signals, `/proc`, nesting. The key page for §3. | §3 |
| [`mount_namespaces(7)`](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html) | Copy semantics, propagation across namespaces, locked mounts. | §4 |
| [`ipc_namespaces(7)`](https://man7.org/linux/man-pages/man7/ipc_namespaces.7.html), [`mq_overview(7)`](https://man7.org/linux/man-pages/man7/mq_overview.7.html), [`shm_overview(7)`](https://man7.org/linux/man-pages/man7/shm_overview.7.html) | IPC isolation and why `/dev/shm` is separate. | §5 |
| [`network_namespaces(7)`](https://man7.org/linux/man-pages/man7/network_namespaces.7.html), [`veth(4)`](https://man7.org/linux/man-pages/man4/veth.4.html) | Network isolation and virtual Ethernet pairs. | §6 |
| [`ip-netns(8)`](https://man7.org/linux/man-pages/man8/ip-netns.8.html), [`ip-link(8)`](https://man7.org/linux/man-pages/man8/ip-link.8.html) | Named namespaces, veth, bridges. | §6 |
| [`user_namespaces(7)`](https://man7.org/linux/man-pages/man7/user_namespaces.7.html) | ID maps, capabilities, ownership, `setgroups`. Essential for §7 and Chapter 05. | §7 |
| [`capabilities(7)`](https://man7.org/linux/man-pages/man7/capabilities.7.html) | "Interaction with user namespaces". | §7 |
| [`newuidmap(1)`](https://man7.org/linux/man-pages/man1/newuidmap.1.html), [`subuid(5)`](https://man7.org/linux/man-pages/man5/subuid.5.html) | How rootless tools map ID ranges. | §7 |
| [`unshare(1)`](https://man7.org/linux/man-pages/man1/unshare.1.html), [`nsenter(1)`](https://man7.org/linux/man-pages/man1/nsenter.1.html), [`lsns(8)`](https://man7.org/linux/man-pages/man8/lsns.8.html) | Tool defaults (propagation, `--fork`, join order). | Labs |
| [`cgroup_namespaces(7)`](https://man7.org/linux/man-pages/man7/cgroup_namespaces.7.html), [`time_namespaces(7)`](https://man7.org/linux/man-pages/man7/time_namespaces.7.html) | The two types not covered in depth here. | §1, Ch. 04 |

## Kernel source (primary)

- [`include/linux/nsproxy.h`](https://elixir.bootlin.com/linux/v6.12/source/include/linux/nsproxy.h),
  [`kernel/nsproxy.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/nsproxy.c)
  — `struct nsproxy`, `copy_namespaces()`, `unshare_nsproxy_namespaces()`, and
  the `setns` syscall implementation.
- [`kernel/utsname.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/utsname.c)
  — a complete, tiny namespace implementation.
- [`kernel/pid_namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/pid_namespace.c)
  — `create_pid_namespace()`, `zap_pid_ns_processes()`.
- [`fs/namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/fs/namespace.c)
  — `copy_mnt_ns()`, `copy_tree()`, locked mounts.
- [`net/core/net_namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/net/core/net_namespace.c)
  — `copy_net_ns()` and the per-subsystem `pernet_operations` that give each
  namespace its own stack.
- [`kernel/user_namespace.c`](https://elixir.bootlin.com/linux/v6.12/source/kernel/user_namespace.c)
  — `create_user_ns()`, `map_write()`, `new_idmap_permitted()`.

## Go standard library (primary for §8)

- [`syscall.SysProcAttr`](https://pkg.go.dev/syscall#SysProcAttr) documentation.
- [`src/syscall/exec_linux.go`](https://github.com/golang/go/blob/master/src/syscall/exec_linux.go)
  — `forkAndExecInChild1`: raw `clone`, the pipe that makes the child wait, and
  the parent-side ID map writing.

## Specifications and project documentation (for connections)

- [CNI specification](https://github.com/containernetworking/cni/blob/main/SPEC.md)
  and the [bridge plugin](https://www.cni.dev/plugins/current/main/bridge/)
  — how Kubernetes networking automates Lab 06.
- Docker documentation: [Bridge network driver](https://docs.docker.com/engine/network/drivers/bridge/)
  and [Packet filtering and firewalls](https://docs.docker.com/engine/network/packet-filtering-firewalls/).
- Kubernetes documentation: [User Namespaces](https://kubernetes.io/docs/concepts/workloads/pods/user-namespaces/),
  [Share Process Namespace between Containers in a Pod](https://kubernetes.io/docs/tasks/configure-pod-container/share-process-namespace/).
- [rootlesscontaine.rs](https://rootlesscontaine.rs/) — how rootless runtimes
  combine user namespaces, `newuidmap`, and user-mode networking.
- nftables wiki: [Performing Network Address Translation (NAT)](https://wiki.nftables.org/wiki-nftables/index.php/Performing_Network_Address_Translation_(NAT)).

## Secondary sources (selected)

- Michael Kerrisk, LWN, "Namespaces in operation" series (2013):
  [part 1 overview](https://lwn.net/Articles/531114/),
  [part 2 API](https://lwn.net/Articles/531381/),
  [part 3 PID](https://lwn.net/Articles/531419/),
  [part 4 more PID](https://lwn.net/Articles/532748/),
  [part 5 user](https://lwn.net/Articles/532593/),
  [part 6 more user](https://lwn.net/Articles/540087/),
  [part 7 network](https://lwn.net/Articles/580893/).
  The classic hands-on introduction by the man-pages maintainer. Predates cgroup
  and time namespaces and some user namespace restrictions; cross-check details
  with current man pages.
- Michael Kerrisk, LWN, ["Mount namespaces and shared subtrees"](https://lwn.net/Articles/689856/) (2016).
- Liz Rice, ["Containers From Scratch"](https://www.youtube.com/watch?v=8fi7uSYlOdc)
  (GOTO 2018) — a Go container built live with the same `SysProcAttr` fields as
  Lab 08.
