#!/usr/bin/env bash
# enter.sh — assemble a container-like root: pivot_root + proc/dev/sys +
# masked and read-only paths. Run inside `unshare` (see README).
#
# Usage: unshare --mount --pid --fork --uts --ipc ... enter.sh /path/to/rootfs
set -eu
ROOTFS="${1:?usage: enter.sh ROOTFS}"

mount --make-rprivate /
mount --bind "$ROOTFS" "$ROOTFS"
mkdir -p "$ROOTFS"/{oldroot,proc,sys,dev,dev/pts,dev/shm,tmp}

# Pseudo-filesystems, mounted UNDER the new root before pivoting.
mount -t proc  proc  "$ROOTFS/proc"  2>/dev/null || true
mount -t sysfs -o ro sys "$ROOTFS/sys" 2>/dev/null || true
mount -t tmpfs -o mode=0755 dev "$ROOTFS/dev"
mount -t devpts -o newinstance,ptmxmode=0666 devpts "$ROOTFS/dev/pts" 2>/dev/null || true
mount -t tmpfs -o size=16m shm "$ROOTFS/dev/shm"

# A couple of essential device nodes (needs CAP_MKNOD; works in a user ns too).
for spec in "null c 1 3" "zero c 1 5" "random c 1 8" "urandom c 1 9" "tty c 5 0"; do
    set -- $spec
    mknod -m 0666 "$ROOTFS/dev/$1" "$2" "$3" "$4" 2>/dev/null || true
done

# Mask a sensitive proc file by bind-mounting /dev/null over it.
[ -e "$ROOTFS/proc/kcore" ] && mount --bind /dev/null "$ROOTFS/proc/kcore" 2>/dev/null || true
# Make /proc/sys read-only (self-bind + remount).
if [ -d "$ROOTFS/proc/sys" ]; then
    mount --bind "$ROOTFS/proc/sys" "$ROOTFS/proc/sys" 2>/dev/null || true
    mount -o remount,bind,ro "$ROOTFS/proc/sys" 2>/dev/null || true
fi

cd "$ROOTFS"
pivot_root . oldroot
cd /
umount -l /oldroot
rmdir /oldroot 2>/dev/null || true

echo "=== container-like root ready ==="
echo "hostname: $(hostname 2>/dev/null || cat /etc/hostname)"
echo "mounts:"; cat /proc/mounts 2>/dev/null
echo "pid of this shell: $$"
exec /bin/sh
