# Lab 01 — OverlayFS Basics: Merge, Read, and the Four Directories

## Goal

Produce evidence that:

1. an overlay mount merges lower and upper layers into one view;
2. the top (upper, then leftmost lower) wins on name conflicts;
3. reads come from the highest layer that has the file;
4. the layers are visible in `mountinfo`.

## Prerequisites

- Linux VM with `sudo`, `util-linux`. A modern kernel (OverlayFS built in).
- Read: [1. The union idea](../../../docs/08-overlayfs/01-union-idea.md) and
  [2. OverlayFS by hand](../../../docs/08-overlayfs/02-overlayfs-by-hand.md).

## Setup

```bash
sudo -i
BASE=/tmp/ovl
rm -rf "$BASE"; mkdir -p "$BASE"/{lower1,lower2,upper,work,merged}

# lower1 (bottom): base image
echo "from lower1" > "$BASE/lower1/a.txt"
echo "from lower1" > "$BASE/lower1/b.txt"
echo "from lower1" > "$BASE/lower1/shared.txt"
# lower2 (on top of lower1): a second image layer
echo "from lower2" > "$BASE/lower2/c.txt"
echo "from lower2 WINS" > "$BASE/lower2/shared.txt"
```

## Experiment

### Part A — Mount the overlay

```bash
mount -t overlay overlay \
  -o lowerdir="$BASE/lower2:$BASE/lower1",upperdir="$BASE/upper",workdir="$BASE/work" \
  "$BASE/merged"
ls "$BASE/merged"
```

**Predict first.** Which files appear in `merged`? What does `shared.txt`
contain?

```bash
cat "$BASE/merged/shared.txt"
cat "$BASE/merged/a.txt" "$BASE/merged/c.txt"
```

### Part B — The layers in mountinfo

```bash
grep "$BASE/merged" /proc/self/mountinfo
findmnt "$BASE/merged"
```

### Part C — A read-only overlay (lowers only)

```bash
mkdir -p "$BASE/merged-ro"
mount -t overlay overlay -o lowerdir="$BASE/lower2:$BASE/lower1" "$BASE/merged-ro"
cat "$BASE/merged-ro/shared.txt"
echo x > "$BASE/merged-ro/a.txt" 2>&1; echo "write exit: $?"
umount "$BASE/merged-ro"; rmdir "$BASE/merged-ro"
```

### Cleanup

```bash
umount "$BASE/merged"; rm -rf "$BASE"
exit
```

## Expected observations

**Part A.** `merged` lists `a.txt`, `b.txt`, `c.txt`, `shared.txt`.
`shared.txt` contains `from lower2 WINS` (leftmost lower wins). `a.txt` is
`from lower1`, `c.txt` is `from lower2`.

**Part B.** The `mountinfo` line ends with
`- overlay overlay rw,lowerdir=.../lower2:.../lower1,upperdir=.../upper,workdir=.../work`.
`findmnt` shows FSTYPE `overlay`.

**Part C.** `shared.txt` still resolves to `from lower2 WINS`. The write fails
with `Read-only file system`: with no `upperdir`, the overlay is read-only.

## Why this happens

- OverlayFS merges directory entries from all layers; for a name in several
  layers, the highest layer's version is shown and read.
- Without an `upperdir`, there is nowhere to write, so the mount is read-only.

## Connection to containers

- Part A is exactly how a runtime presents an image: the image's layers are the
  `lowerdir` list, and a per-container `upperdir` receives writes.
- Part C is how a runtime can mount an image read-only for inspection, and how
  shared base layers are used by many containers.
- Part B is how you find, from the host, which layers make up a container's
  rootfs.

## Questions to think about

1. If two images share the base layer `lower1`, how many copies of `lower1` are
   stored on the host? Why is that safe?
2. Reorder the `lowerdir` to `lower1:lower2`. What does `shared.txt` contain now,
   and why?
3. Why must a running container's overlay have an `upperdir`, while an image at
   rest can be mounted without one?
