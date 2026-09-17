# 4. Network and IPC Namespaces

## What we add

`CLONE_NEWIPC` is always on in `minic`; `CLONE_NEWNET` is added when `-net` is
passed:

```go
flags := ... | syscall.CLONE_NEWIPC
if cfg.netns {
    flags |= syscall.CLONE_NEWNET
}
```

No child-side code is strictly required for basic isolation, which is itself the
lesson: creating these namespaces is one flag each; making them *useful* is more
work.

## IPC

`CLONE_NEWIPC` gives the container its own System V IPC identifiers and POSIX
message queues (Chapter 03 §5). `ipcs` inside shows an empty table; segments the
container creates vanish when it exits. Note the caveat from Chapter 03 §5:
**POSIX shared memory lives in `/dev/shm`**, so full isolation also needs a
private `/dev/shm` tmpfs; `minic`'s `setupRootfs` mounts a tmpfs `/dev` but does
not add a separate `/dev/shm` mount, so this is one of its documented
simplifications.

## Network

`CLONE_NEWNET` gives the container its own network stack: only a loopback
device, and it is **down** (Chapter 03 §6).

## What changed

With `-net`:

- `ip addr` (or `ifconfig`) inside shows only `lo`, state DOWN. `ping 127.0.0.1`
  fails until `ip link set lo up` (which needs `CAP_NET_ADMIN` over the net
  namespace — present here because the container still has capabilities, §6 not
  yet applied; or when using `-userns`).
- The container cannot reach anything: no addresses, no routes.
- Ports are isolated: the container could bind `0.0.0.0:8080` even if the host
  already listens on 8080.

With IPC (always):

- `ipcs -m` is empty; creating a segment inside does not appear on the host.

## What has NOT changed

- With `-net`, the container is **isolated but disconnected**. `minic` does
  **not** create a veth pair or bridge (Chapter 03 §6 / Lab 06 did that by hand);
  wiring the namespace to the outside is the job a CNI plugin or Docker's
  libnetwork does, and is deliberately out of scope. Without `-net`, the
  container shares the host network (like `docker run --network host`).
- Resources, privilege, syscalls: still unchanged (§5, §6 next).

## Why minic stops here for networking

Connecting a network namespace is a substantial subsystem (veth, bridge,
addressing, NAT, DNS). Chapter 03 Lab 06 built it by hand; production does it in
libnetwork or CNI. Keeping `minic` to "create the namespace, leave it isolated"
keeps the runtime readable while still demonstrating the isolation primitive.
This is exactly the division of labor Chapters 12–13 describe: the low-level
runtime creates the namespace; a higher layer wires it up.

## Further Reading

- Chapter 03 [§5 IPC](../03-namespaces/05-ipc-namespace.md) and
  [§6 network](../03-namespaces/06-network-namespace.md); Lab 06 builds a bridge.
- CNI: [SPEC.md](https://github.com/containernetworking/cni/blob/main/SPEC.md)
  — how a plugin receives a network-namespace path and wires it (Ch. 03 §6).
