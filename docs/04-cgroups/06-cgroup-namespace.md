# 6. The cgroup Namespace

Chapter 03 postponed this namespace because it virtualizes something that had
not been introduced yet: the cgroup hierarchy. With sections 2–5 in place, it is
short.

## 1. The global resource before isolation

`/proc/<pid>/cgroup` shows a process's cgroup as a path from the **root of the
whole hierarchy**:

```text
0::/system.slice/containerd.service/kubepods-burstable-pod1234.slice/cri-containerd-abcd.scope
```

Without a cgroup namespace, a process inside a container therefore sees:

- the **host's cgroup layout**, which leaks information (other pod names,
  service names, the node's structure);
- a path that is **not portable**: software that reads its own cgroup path and
  then opens `/sys/fs/cgroup/<that path>/memory.max` works only if the cgroup
  filesystem inside the container is mounted exactly like the host's.

## 2. What the namespace isolates

A cgroup namespace records a **cgroup root**: the cgroup the creating process
was in at the moment the namespace was created. For processes in the namespace:

- `/proc/<pid>/cgroup` shows paths **relative to that root**;
- a `cgroup2` filesystem **mounted from inside** the namespace has that cgroup as
  its root directory, so `/sys/fs/cgroup` inside the container *is* the
  container's own cgroup.

It does **not** change which cgroup a process is in, nor any limit. It only
changes how cgroup paths are *presented*.

## 3. What changes from the process's perspective

```text
host view (initial cgroup namespace)
/proc/4242/cgroup:        0::/system.slice/docker-abcd.scope

container view (new cgroup namespace, created while in docker-abcd.scope)
/proc/self/cgroup:        0::/
/sys/fs/cgroup/           (mounted inside) → contents of docker-abcd.scope
/sys/fs/cgroup/memory.max → the container's own limit
```

If a process in the namespace is moved **outside** the namespace root (for
example, to a sibling cgroup), its path is shown with `..` components, such as
`0::/../other.scope`.

## 4. Kernel API

`clone(CLONE_NEWCGROUP)`, `unshare(CLONE_NEWCGROUP)`, `setns(fd,
CLONE_NEWCGROUP)`. The namespace root is fixed at creation, so **order matters**:
a runtime must move the process into its cgroup **before** creating the cgroup
namespace. Otherwise the root would be the runtime's own cgroup.

## 5. Shell experiment

```bash
sudo mkdir /sys/fs/cgroup/labns
sudo bash -c 'echo $$ > /sys/fs/cgroup/labns/cgroup.procs; exec unshare --cgroup --mount bash -c "cat /proc/self/cgroup; mount -t cgroup2 none /sys/fs/cgroup; ls /sys/fs/cgroup | head -n 5"'
```

## 6. How container runtimes use it

- Docker on cgroup v2 hosts and Kubernetes (containerd/CRI-O) create a cgroup
  namespace for each container by default (Docker's `--cgroupns=private`), after
  placing the container process into its cgroup.
- The runtime mounts `cgroup2` at `/sys/fs/cgroup` inside the container, usually
  **read-only**, so the application can read its limits but not raise them.
- This is what makes the JVM's container detection (section 7) simple on v2:
  inside the container, `/proc/self/cgroup` says `0::/`, and the limits are at
  `/sys/fs/cgroup/memory.max` and `/sys/fs/cgroup/cpu.max`.

## Evidence

Lab: [`lab-06-cgroup-namespace-and-jvm`](../../labs/04-cgroups/lab-06-cgroup-namespace-and-jvm/), Parts A–B.

## Further Reading

- [`cgroup_namespaces(7)`](https://man7.org/linux/man-pages/man7/cgroup_namespaces.7.html)
  — the complete semantics, including `..` paths and the reason the namespace
  was added (information leaks and container migration).
- Kernel docs: [Control Group v2 — Namespace](https://docs.kernel.org/admin-guide/cgroup-v2.html#namespace)
  — interaction with delegation and the `nsdelegate` mount option.
- Docker documentation: [`docker run --cgroupns`](https://docs.docker.com/reference/cli/docker/container/run/)
  — the `host` and `private` modes and their defaults on cgroup v1 vs v2.
