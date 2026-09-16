# Labs — Chapter 02: Linux Filesystem

All labs require Linux and, except Lab 01, `sudo`. Every lab works in a scratch
directory under `/tmp` and ends with a cleanup step. Mounts made here affect
the whole host (there are no mount namespaces yet), so use a disposable VM.

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-inodes-and-path-resolution](lab-01-inodes-and-path-resolution/) | inodes vs names, deleted open files, rename vs in-place write, mount crossing | No | [§1](../../docs/02-linux-filesystem/01-vfs-and-path-resolution.md) |
| [lab-02-mounts-and-mountinfo](lab-02-mounts-and-mountinfo/) | hiding, stacking, `mountinfo`, `noexec`/`ro`, busy and lazy unmount | Yes | [§2](../../docs/02-linux-filesystem/02-mounts.md) |
| [lab-03-bind-mounts](lab-03-bind-mounts/) | bind, rbind, read-only binds, file bind mounts | Yes | [§3](../../docs/02-linux-filesystem/03-bind-mounts.md) |
| [lab-04-mount-propagation](lab-04-mount-propagation/) | shared, slave, private, unbindable | Yes | [§4](../../docs/02-linux-filesystem/04-mount-propagation.md) |
| [lab-05-special-filesystems](lab-05-special-filesystems/) | device nodes, `nodev`, procfs/sysfs instances, devpts, tmpfs limits | Yes | [§5](../../docs/02-linux-filesystem/05-special-filesystems.md) |

If a lab is interrupted, list leftover mounts with `findmnt -R /tmp` and
remove them with `sudo umount -R <path>`.

Expected observations were written against Linux 6.x and util-linux 2.38+.
Details such as error message wording can differ by version.
