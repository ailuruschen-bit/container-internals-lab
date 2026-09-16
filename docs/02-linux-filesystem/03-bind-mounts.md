# 3. Bind Mounts

## The problem: the same directory in two places

Suppose a directory `/srv/app-data` already exists on the host's ext4
filesystem, and a process should see it at `/data`. Options without bind
mounts are poor:

- a **symbolic link** `/data -> /srv/app-data` is resolved by path, so it breaks
  (or points somewhere else) when the process has a different root directory;
- **copying** creates two independent versions;
- **mounting the whole device** again exposes the entire filesystem, not just
  one directory.

## The Linux abstraction: a bind mount

A **bind mount** creates a new mount whose root is an *existing dentry* from an
*existing mount*. The result is the same files, reachable through another path.

```bash
mount --bind /srv/app-data /data
```

```c
mount("/srv/app-data", "/data", NULL, MS_BIND, NULL);
```

Using the vocabulary from [section 2](02-mounts.md):

```text
existing mount [1]  ext4 /dev/vda1  root "/"       at /
new mount      [7]  ext4 /dev/vda1  root "/srv/app-data"  at /data
                    └── same super_block, different root dentry, different mount point
```

The `mountinfo` line of the bind mount shows the source directory in field 4
(root), which is how you can recognize a bind mount:

```text
7 1 252:1 /srv/app-data /data rw,relatime shared:1 - ext4 /dev/vda1 rw
```

Properties that follow from this model:

- **No copy, no link.** Writes through `/data/x` and `/srv/app-data/x` modify
  the same inode.
- **Resolved once.** The source path is resolved when the bind mount is
  created. Afterwards the mount refers to a dentry, not a string. Moving the
  process's root does not break it. This is why bind mounts, not symlinks, are
  used to bring host directories into containers.
- **Files can be bind-mounted too.** Source and target must both be files. The
  mount pins the **inode** the source name pointed to at that moment. If the
  source is later replaced by `rename()`, the bind mount still shows the old
  inode (Lab 01 Part D explained this).
- **Directory contents at the target are hidden**, as for any mount.

## Recursive vs non-recursive

A plain bind mount (`MS_BIND`) copies only the one mount at the source. If other
filesystems are mounted *below* the source directory, they are not included;
the target shows the underlying, usually empty, directories instead.

`MS_BIND | MS_REC` (`mount --rbind`) copies the source mount **and all mounts
below it**.

```text
source tree                    --bind /  /mnt/x         --rbind /  /mnt/x
/          (ext4)              /mnt/x       ext4 root    /mnt/x        ext4 root
├── proc   (proc)              /mnt/x/proc  empty dir    /mnt/x/proc   proc
└── sys    (sysfs)             /mnt/x/sys   empty dir    /mnt/x/sys    sysfs
```

Recursive bind mounts are powerful and dangerous: `--rbind /` exposes every
host mount, including `/proc`, `/sys`, and `/dev`.

## Read-only bind mounts

Because `ro` is a **per-mount** option, a bind mount can make a directory
read-only in one place while it stays writable in the original location.

Historically this required two system calls, because the kernel ignores most
flags on the initial `MS_BIND`:

```c
mount("/srv/config", "/config", NULL, MS_BIND, NULL);
mount(NULL, "/config", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL);
```

Modern `mount(8)` performs the second step automatically for
`mount --bind -o ro`. With `--rbind`, the remount applies only to the top
mount; submounts stay writable unless each is remounted (or the newer
`mount_setattr()` with `AT_RECURSIVE` is used). Container runtimes handle this
explicitly, and it has been the source of real bugs.

## Security considerations

A bind mount only changes **where** files are visible. It does not change file
ownership, permission bits, or which UID the kernel compares against them.
When a host directory is bind-mounted into a container:

- UID 1000 in the container is UID 1000 on the host (Chapter 01 §7), unless a
  user namespace or an idmapped mount remaps it (Chapter 05);
- a writable bind mount of a sensitive host path (for example `/`, `/etc`, or
  the container runtime's socket) is effectively host access.

## Why this matters for containers

Bind mounts are everywhere in containers:

| Container feature | Bind mount |
|---|---|
| Volumes / host paths (`-v /host:/container`) | directory bind mount, optionally `ro` |
| `/etc/hosts`, `/etc/hostname`, `/etc/resolv.conf` | single-file bind mounts prepared by the engine |
| Kubernetes Secrets and ConfigMaps | bind mounts of tmpfs-backed directories prepared by the kubelet |
| Making the new root a mount point before `pivot_root` | bind-mounting the rootfs onto itself (Chapter 07) |
| Read-only paths such as `/proc/sys` | a bind mount of the path onto itself, remounted `ro` |
| Masked paths such as `/proc/kcore` | bind-mounting `/dev/null` (file) or mounting an empty read-only tmpfs over the path |

The last two rows show a technique you will see in runc: **bind-mounting a path
onto itself** just to create a separate mount whose per-mount options can be
changed.

## Evidence

Lab: [`lab-03-bind-mounts`](../../labs/02-linux-filesystem/lab-03-bind-mounts/)

## Further Reading

- [`mount(2)`](https://man7.org/linux/man-pages/man2/mount.2.html), section
  "Creating a bind mount" — which flags are honored at bind time and why
  read-only needs a remount.
- [`mount(8)`](https://man7.org/linux/man-pages/man8/mount.8.html), section
  "Bind mount operation" — the user-level semantics, including the note that
  `--bind` is not recursive and how `-o ro` is applied.
- [`mount_setattr(2)`](https://man7.org/linux/man-pages/man2/mount_setattr.2.html)
  — the modern way to change options recursively and atomically, including
  idmapped mounts. Read the rationale in the DESCRIPTION.
- Kubernetes documentation, [ConfigMaps: mounted ConfigMaps are updated automatically](https://kubernetes.io/docs/concepts/configuration/configmap/#mounted-configmaps-are-updated-automatically)
  — note the warning that a ConfigMap used as a `subPath` mount does not receive
  updates. After this section you can explain why: a single-file bind mount
  pins one inode, while the kubelet updates ConfigMaps by atomically swapping a
  symlink to a new directory.
