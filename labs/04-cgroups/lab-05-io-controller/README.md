# Lab 05 — The I/O Controller: io.max for Direct and Buffered Writes

## Goal

Produce evidence that:

1. `io.max` is configured per **whole block device** using `MAJ:MIN`;
2. direct I/O is delayed to the configured rate, not failed;
3. buffered writes are also throttled when memory and I/O controllers are both
   enabled, but the delay may appear at a different point (`write` vs `fsync`);
4. `io.stat` accounts reads and writes per device.

## Prerequisites

- Linux VM with cgroup v2, `sudo`, `util-linux` (`lsblk`, `findmnt`), a
  filesystem that supports cgroup writeback (ext4 or xfs) at `/var/tmp`.
- About 300 MB of free disk space.
- Read: [5. The PIDs and I/O controllers](../../../docs/04-cgroups/05-pids-and-io-controllers.md), Part 2.

## Setup

```bash
sudo -i
CG=/sys/fs/cgroup
echo "+io +memory" > $CG/cgroup.subtree_control
mkdir -p $CG/labio
incg() { local cg=$1; shift; bash -c 'echo $$ > "$0/cgroup.procs"; exec "$@"' "$CG/$cg" "$@"; }

SRC=$(findmnt -no SOURCE -T /var/tmp)          # e.g. /dev/vda1 or /dev/mapper/...
DISK=$(lsblk -no PKNAME "$SRC" | head -n1)      # parent disk, e.g. vda (empty if SRC is a whole disk)
DEV=/dev/${DISK:-$(basename "$SRC")}
MAJMIN=$(lsblk -dno MAJ:MIN "$DEV" | tr -d ' ')
echo "filesystem on $SRC, disk $DEV, MAJ:MIN $MAJMIN"
```

`io.max` rejects partitions; it needs the whole disk. On LVM or device-mapper
setups, `SRC` itself (the `dm-N` device) is usually the right target; if writing
`io.max` fails with `No such device`, try the `MAJ:MIN` of `SRC` directly.

## Experiment

### Part A — Baseline

```bash
incg labio dd if=/dev/zero of=/var/tmp/labio.dat bs=1M count=100 oflag=direct status=progress
cat $CG/labio/io.stat
```

### Part B — Limit direct writes to 10 MB/s

**Predict first.** How long will writing 100 MB with `O_DIRECT` take? Will `dd`
report an error?

```bash
echo "$MAJMIN wbps=10485760" > $CG/labio/io.max
cat $CG/labio/io.max
time incg labio dd if=/dev/zero of=/var/tmp/labio.dat bs=1M count=100 oflag=direct status=progress
```

### Part C — Limit read IOPS

```bash
echo 1 > /proc/sys/vm/drop_caches
echo "$MAJMIN riops=50" > $CG/labio/io.max
cat $CG/labio/io.max
time incg labio dd if=/var/tmp/labio.dat of=/dev/null bs=4k count=500 iflag=direct status=none
```

### Part D — Buffered writes

```bash
echo "$MAJMIN wbps=10485760 riops=max" > $CG/labio/io.max
echo 3 > /proc/sys/vm/drop_caches
time incg labio dd if=/dev/zero of=/var/tmp/labio.dat bs=1M count=100 status=none
time incg labio sync -f /var/tmp/labio.dat
time incg labio dd if=/dev/zero of=/var/tmp/labio.dat bs=1M count=100 conv=fsync status=none
cat $CG/labio/io.stat
```

**Predict first.** Which of the three commands will take about 10 seconds?

### Cleanup

```bash
rm -f /var/tmp/labio.dat
rmdir $CG/labio
exit
```

## Expected observations

**Part A.** `dd` completes at the disk's native speed (often hundreds of MB/s in
a VM). `io.stat` shows a line for `$MAJMIN` with `wbytes` about 104857600.

**Part B.** `dd` succeeds, with a throughput of about `10.0 MB/s`, and `real`
time about 10 seconds. No error is reported.

**Part C.** 500 direct reads at 50 IOPS take about 10 seconds.

**Part D.** The results depend on dirty-page settings and memory size, but the
typical pattern is: the plain `dd` returns quickly (data only went into the page
cache, or is throttled partially through dirty-page balancing); `sync` then takes
several seconds; `dd conv=fsync` takes about 10 seconds in total. `io.stat`
shows the writeback I/O charged to `labio`, not to the kernel flusher threads.

## Why this happens

- **B, C.** `blk-throttle` holds bios from the cgroup in a queue and dispatches
  them at the configured rate.
- **D.** Buffered writes are recorded as dirty pages owned by the cgroup's
  memory controller. Writeback of those pages is tagged with the owning cgroup
  (cgroup writeback), so the I/O controller throttles it, and dirty-page
  balancing slows further writers from that cgroup. This attribution only works
  because memory and I/O share one hierarchy in v2.

## Connection to containers

- Part B is `docker run --device-write-bps`.
- Part D explains why a disk-heavy container with an I/O limit can show latency
  in `fsync()` or in seemingly unrelated writes, and why the effect depends on
  memory limits too.

## Questions to think about

1. Why does the I/O controller need the memory controller to throttle buffered
   writes correctly?
2. A container's overlay filesystem is on `/var/lib/docker`, which is on
   `/dev/nvme1n1`, while its volume is on `/dev/nvme0n1`. Which `io.max` lines
   are needed to limit all its writes?
3. Why might throttling I/O indirectly increase a container's memory usage?
