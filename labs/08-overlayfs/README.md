# Labs — Chapter 08: OverlayFS

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-overlay-basics](lab-01-overlay-basics/) | merge, top-wins, read-only overlay, mountinfo | Yes | [§1](../../docs/08-overlayfs/01-union-idea.md), [§2](../../docs/08-overlayfs/02-overlayfs-by-hand.md) |
| [lab-02-copyup-and-whiteout](lab-02-copyup-and-whiteout/) | copy-up, whiteouts, opaque dirs, copy-up cost | Yes | [§2](../../docs/08-overlayfs/02-overlayfs-by-hand.md) |
| [lab-03-image-layers](lab-03-image-layers/) | real image layers = overlay layers; upperdir = diff | Docker | [§3](../../docs/08-overlayfs/03-image-layers.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y util-linux coreutils attr
# lab 03 also needs: docker (or podman) and jq
```

Labs 01–02 can also run **rootless** inside `unshare --user --map-root-user
--mount` on Linux 5.11+ (unprivileged overlay mounts). Expected observations were
written for Linux 6.x; copy-up timing and exact `overlay2` paths vary.
