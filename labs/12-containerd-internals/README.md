# Labs — Chapter 12: containerd Internals

| Lab | Topic | Needs | Doc section |
|---|---|---|---|
| [lab-01-ctr-and-services](lab-01-ctr-and-services/) | the daemon, plugins, containerd namespaces, images/content/snapshots/containers/tasks | containerd, ctr, root | [§2](../../docs/12-containerd-internals/02-architecture.md) |
| [lab-02-content-and-snapshots](lab-02-content-and-snapshots/) | blobs by digest; committed vs active snapshots; the overlay mount | containerd, ctr, root | [§3](../../docs/12-containerd-internals/03-content-and-snapshots.md) |
| [lab-03-tasks-and-shim](lab-03-tasks-and-shim/) | shim as parent; runc exits; survive a daemon restart; reaping | containerd, ctr, **disposable VM** | [§4](../../docs/12-containerd-internals/04-tasks-and-shim.md), [§5](../../docs/12-containerd-internals/05-traced-path.md) |

Setup:

```bash
sudo apt-get install -y containerd jq procps
sudo systemctl enable --now containerd
# A Docker host already has containerd; use `ctr -n moby ...` to see Docker's objects.
```

Lab 03 restarts containerd — use a throwaway VM. Observations were written for
containerd 1.7 / 2.0; `ctr` output columns and paths vary by version.
