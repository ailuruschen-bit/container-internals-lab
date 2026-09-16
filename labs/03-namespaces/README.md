# Labs — Chapter 03: Namespaces

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-namespace-files-and-tools](lab-01-namespace-files-and-tools/) | `/proc/<pid>/ns`, `lsns`, `nsenter`, pinning a namespace | Yes | [§1](../../docs/03-namespaces/01-namespace-concepts.md) |
| [lab-02-uts-namespace](lab-02-uts-namespace/) | `clone`/`unshare`/`setns` in C and shell | Yes | [§2](../../docs/03-namespaces/02-uts-namespace.md) |
| [lab-03-pid-namespace](lab-03-pid-namespace/) | NSpid, `--fork`, `/proc`, orphans, PID 1 signals, init death | Yes | [§3](../../docs/03-namespaces/03-pid-namespace.md) |
| [lab-04-mount-namespace](lab-04-mount-namespace/) | private trees, shared files, propagation across namespaces | Yes | [§4](../../docs/03-namespaces/04-mount-namespace.md) |
| [lab-05-ipc-namespace](lab-05-ipc-namespace/) | System V IPC, mqueue mounts, IPC sysctls, `/dev/shm` | Yes | [§5](../../docs/03-namespaces/05-ipc-namespace.md) |
| [lab-06-network-namespace](lab-06-network-namespace/) | veth, bridge, masquerade, DNAT, shared `localhost` | Yes | [§6](../../docs/03-namespaces/06-network-namespace.md) |
| [lab-07-user-namespace](lab-07-user-namespace/) | ID maps, scoped capabilities, unprivileged namespaces | No | [§7](../../docs/03-namespaces/07-user-namespace.md) |
| [lab-08-combining-namespaces](lab-08-combining-namespaces/) | Go `SysProcAttr`, `nsenter --all`, what has not changed | Partly | [§8](../../docs/03-namespaces/08-combining-namespaces.md) |

Tools for this chapter on Debian/Ubuntu:

```bash
sudo apt-get install -y util-linux iproute2 nftables procps psmisc strace gcc golang curl iputils-ping tcpdump libcap2-bin
```

Namespaces created by `unshare` disappear when their last process exits, so
most labs need no cleanup. Lab 06 has a `teardown.sh`; Lab 01 and Lab 04 leave
empty directories that you can remove.

Expected observations were written for Linux 6.x, util-linux 2.38+, and
iproute2 6.x.
