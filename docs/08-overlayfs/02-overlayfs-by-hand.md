# 2. OverlayFS by Hand

## The four directories

OverlayFS is mounted with `mount -t overlay`, and it takes its layers as mount
options. Four directories play distinct roles:

| Directory | Role |
|---|---|
| **lowerdir** | one or more **read-only** layers, listed top-to-bottom, colon-separated |
| **upperdir** | the single **writable** layer; all changes land here |
| **workdir** | an empty working directory OverlayFS needs for atomic internal operations (copy-up, whiteout creation); it must be on the **same filesystem** as `upperdir` and must not be touched by you |
| **merged** | the mount point where the combined view appears |

```bash
mount -t overlay overlay \
  -o lowerdir=/lower2:/lower1,upperdir=/upper,workdir=/work \
  /merged
```

Notes that trip people up:

- In `lowerdir`, the **leftmost** path is the **highest** (it wins on conflicts).
  `lowerdir=L2:L1` means L2 shadows L1.
- `upperdir` and `workdir` must be on the **same** filesystem, and it must
  support the needed features (ext4, xfs with `ftype=1`, tmpfs for testing).
  They cannot be on separate filesystems, and `workdir` must be empty.
- A **read-only** overlay can be mounted with `lowerdir` only (no `upperdir`,
  no `workdir`); several lowers are simply merged. Images that are not running as
  a container are often mounted this way.

## What each operation does

Set up a tiny overlay and watch where things land. (The lab runs this; here is
the model.)

```text
lower/                    upper/ (empty)         merged/  (mounted)
  a.txt "L"                                         a.txt "L"
  b.txt "L"                                         b.txt "L"
```

| Operation in `merged/` | Effect underneath |
|---|---|
| `cat merged/a.txt` | read from `lower/a.txt`; `upper` unchanged |
| `echo X > merged/a.txt` | **copy-up**: `lower/a.txt` copied to `upper/a.txt`, then overwritten with `X`. `lower/a.txt` still `"L"` |
| `echo Y > merged/new.txt` | new file created only in `upper/new.txt` |
| `rm merged/b.txt` | a **whiteout** appears at `upper/b.txt` (see below); `lower/b.txt` still `"L"` |
| `mkdir merged/d` | new directory in `upper/d` |
| `chmod 700 merged/a.txt` | triggers copy-up (metadata change needs a writable copy) |

After these, the layers look like:

```text
lower/                    upper/                        merged/
  a.txt "L"                 a.txt "X" (copied up)         a.txt "X"
  b.txt "L"                 b.txt  (whiteout, hides L)     new.txt "Y"
                            new.txt "Y"                    d/
                            d/
```

## What a whiteout actually is

An OverlayFS whiteout is a **character device with device number 0/0**
(`mknod name c 0 0`). When OverlayFS sees such a node in the upper layer, it
treats the same-named file in the lower layers as deleted and hides it from the
merged view.

```console
$ ls -l upper/
c--------- 1 root root 0, 0 ... b.txt        # <- the whiteout: char device 0/0
-rw-r--r-- 1 root root      2 ... a.txt
-rw-r--r-- 1 root root      2 ... new.txt
```

An **opaque directory** (a directory that should hide everything in the lower
layer with the same name, for example after `rm -rf merged/d; mkdir merged/d`) is
marked with the extended attribute `trusted.overlay.opaque="y"` on the upper
directory. You can see it with `getfattr -n trusted.overlay.opaque upper/d`
(needs privilege to read `trusted.*` xattrs).

These on-disk markers are why you should never write to `upperdir` directly; let
OverlayFS manage it through the `merged` mount.

## Reading the merged mount

`/proc/self/mountinfo` shows an overlay mount with all its layers:

```text
... - overlay overlay rw,lowerdir=/lower2:/lower1,upperdir=/upper,workdir=/work
```

So from the host you can see exactly which layers make up any container's rootfs
(a real container's line lists long, hashed layer paths under the storage
driver's directory).

## Metacopy and other refinements

Modern OverlayFS has optimizations you may see:

- **metacopy** (`redirect_dir`/`metacopy=on`): a metadata-only change (like
  `chmod` or `chown`) copies up only the file's metadata, not its data, deferring
  the expensive data copy until the data is actually written. This reduces
  copy-up cost, important for `chown`-heavy image extraction.
- **Multiple lower layers** are extremely common: an image with 10 layers is one
  overlay mount with 10 `lowerdir` entries.
- **Nested/stacked overlays** are restricted; runtimes avoid mounting an overlay
  whose lower is another overlay in ways the kernel forbids.

## Why this matters for containers

- This is literally how a container's rootfs is assembled: the runtime mounts an
  overlay with the image layers as `lowerdir` and a fresh per-container
  `upperdir`, then uses that `merged` directory as the rootfs for `pivot_root`
  (Chapter 07).
- Copy-up and whiteouts explain container behavior: editing a large image file is
  slow the first time; deleting an image file inside a container does not shrink
  the image; the writable layer only holds diffs.
- Seeing the overlay line in `mountinfo` lets you find exactly which layers a
  container uses and where its writable layer is on the host.

## Evidence

- Lab: [`lab-01-overlay-basics`](../../labs/08-overlayfs/lab-01-overlay-basics/)
- Lab: [`lab-02-copyup-and-whiteout`](../../labs/08-overlayfs/lab-02-copyup-and-whiteout/)

## Further Reading

- Kernel docs: [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html)
  — read "Multiple lower layers", "Non-directories" (copy-up), "Whiteouts and
  opaque directories", "workdir", and "Metadata only copy up". The complete
  specification of everything in this section.
- [`mount(8)`](https://man7.org/linux/man-pages/man8/mount.8.html), the overlay
  section — the `lowerdir`/`upperdir`/`workdir` options.
- [`mknod(1)`](https://man7.org/linux/man-pages/man1/mknod.1.html) — how a
  whiteout (a 0/0 char device) is created.
- LWN, ["Overlayfs and the trouble with ..."](https://lwn.net/Articles/671616/)
  and the broader overlayfs coverage — design discussions and corner cases.
