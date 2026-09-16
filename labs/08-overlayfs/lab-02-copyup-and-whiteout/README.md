# Lab 02 — Copy-up and Whiteouts: Where Changes Actually Land

## Goal

Produce evidence that:

1. modifying a lower-layer file triggers **copy-up** into the upper layer, while
   the lower file stays unchanged;
2. deleting a lower-layer file creates a **whiteout** (a 0/0 char device) in the
   upper layer;
3. new files and directories are created only in the upper layer;
4. the merged view reflects all of this as an ordinary filesystem;
5. copy-up of a large file has a measurable first-write cost.

## Prerequisites

- Linux VM with `sudo`, `util-linux`, `coreutils`.
- Read: [2. OverlayFS by hand](../../../docs/08-overlayfs/02-overlayfs-by-hand.md).

## Setup

```bash
sudo -i
BASE=/tmp/ovl2
rm -rf "$BASE"; mkdir -p "$BASE"/{lower,upper,work,merged}
echo "original in lower" > "$BASE/lower/a.txt"
echo "original in lower" > "$BASE/lower/b.txt"
dd if=/dev/zero of="$BASE/lower/big.dat" bs=1M count=200 status=none
mount -t overlay overlay -o lowerdir="$BASE/lower",upperdir="$BASE/upper",workdir="$BASE/work" "$BASE/merged"
snapshot() { echo "--- upper:"; ls -l "$BASE/upper"; echo "--- lower a.txt: $(cat "$BASE/lower/a.txt")"; }
snapshot
```

## Experiment

### Part A — Copy-up on modify

**Predict first.** After `echo modified > merged/a.txt`, what is in `upper/`, and
what does `lower/a.txt` still contain?

```bash
echo "modified via merged" > "$BASE/merged/a.txt"
cat "$BASE/merged/a.txt"
snapshot
```

### Part B — New file goes to upper only

```bash
echo "brand new" > "$BASE/merged/new.txt"
ls "$BASE/upper" "$BASE/lower"
```

### Part C — Delete creates a whiteout

```bash
rm "$BASE/merged/b.txt"
ls "$BASE/merged" | grep b.txt || echo "b.txt gone from merged"
ls -l "$BASE/upper/b.txt"                 # look at the file type
cat "$BASE/lower/b.txt"                    # still there underneath
```

**Predict first.** What kind of file will `upper/b.txt` be?

### Part D — Opaque directory

```bash
mkdir "$BASE/lower/d"; echo hi > "$BASE/lower/d/inside.txt"
umount "$BASE/merged"
mount -t overlay overlay -o lowerdir="$BASE/lower",upperdir="$BASE/upper",workdir="$BASE/work" "$BASE/merged"
ls "$BASE/merged/d"
rm -rf "$BASE/merged/d"; mkdir "$BASE/merged/d"; echo new > "$BASE/merged/d/only-new.txt"
ls "$BASE/merged/d"
getfattr -n trusted.overlay.opaque "$BASE/upper/d" 2>&1 | tail -n1
```

### Part E — Copy-up cost

```bash
sync; echo 3 > /proc/sys/vm/drop_caches
echo "first byte write (triggers copy-up of 200 MB):"
time dd if=/dev/zero of="$BASE/merged/big.dat" bs=1 count=1 conv=notrunc status=none
echo "second write (already copied up):"
time dd if=/dev/zero of="$BASE/merged/big.dat" bs=1 count=1 conv=notrunc status=none
ls -lh "$BASE/upper/big.dat"
```

### Cleanup

```bash
umount "$BASE/merged"; rm -rf "$BASE"
exit
```

## Expected observations

**Part A.** `merged/a.txt` is `modified via merged`. `upper/` now contains
`a.txt` with the new content. `lower/a.txt` still contains `original in lower`.

**Part B.** `new.txt` is in `upper/`, not in `lower/`.

**Part C.** `b.txt` is absent from `merged`. `ls -l upper/b.txt` shows a
character device: `c--------- ... 0, 0 ... b.txt`. `lower/b.txt` still contains
its original text.

**Part D.** Before removal, `merged/d` shows `inside.txt`. After
`rm -rf; mkdir; echo`, `merged/d` shows only `only-new.txt` (the lower `d` is
hidden). `getfattr` prints `trusted.overlay.opaque="y"` on `upper/d`.

**Part E.** The first 1-byte write takes noticeably longer (it copies the whole
200 MB into `upper`), the second is fast. `upper/big.dat` is ~200 MB.

## Why this happens

- **A, E.** A write to a lower-only file triggers copy-up: OverlayFS copies the
  file into `upperdir` (via `workdir` for atomicity), then applies the write.
- **B.** New files have no lower version, so they are created directly in `upper`.
- **C.** OverlayFS cannot delete from a read-only lower, so it records the
  deletion as a 0/0 char-device whiteout in `upper`.
- **D.** Replacing a lower directory marks the new upper directory opaque, so the
  lower directory's contents are hidden.

## Connection to containers

- Part A/E is why editing a large file that came from the image is slow the first
  time in a container, and why the writable layer grows by the full file size.
- Part C is why `rm`-ing files from the base image inside a container does **not**
  shrink the image or reclaim its space; the data stays in the lower layer.
- These `upper` contents are precisely the container's diff; `docker diff` reports
  exactly the copy-ups, new files, and whiteouts you produced here.

## Questions to think about

1. A container deletes a 1 GB file that came from the image to "save space". Does
   the container's disk usage go down? Where does the data still live?
2. Why is copying an entire file on the first write (copy-up) acceptable in
   practice? When is it a problem, and what is `metacopy`?
3. `docker diff <container>` shows `C /a.txt`, `A /new.txt`, `D /b.txt`. Map each
   to what you saw in `upper/`.
