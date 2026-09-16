#!/usr/bin/env bash
# pivot.sh — enter a rootfs with pivot_root inside a new mount namespace.
#
# Run this INSIDE `unshare` (see the README). It assumes it is already in a new
# mount namespace and (for a rootless run) a user namespace.
#
# Usage: unshare ... pivot.sh /path/to/rootfs
set -eu
ROOTFS="${1:?usage: pivot.sh ROOTFS}"

# 1. Stop mount events from propagating to the host (Chapter 03 section 4).
mount --make-rprivate /

# 2. pivot_root requires new_root to be a mount point: bind the rootfs onto itself.
mount --bind "$ROOTFS" "$ROOTFS"

# 3. Prepare put_old and the pseudo-filesystems a process expects.
mkdir -p "$ROOTFS/oldroot" "$ROOTFS/proc" "$ROOTFS/sys" "$ROOTFS/dev"

cd "$ROOTFS"
# 4. Swap the root mount; the old root moves to ./oldroot.
pivot_root . oldroot
cd /

# 5. Mount a fresh /proc (this needs a PID namespace to be useful; see README).
mount -t proc proc /proc 2>/dev/null || echo "(mounting /proc needs a PID namespace; skipped)"

# 6. Detach the old host root so it becomes unreachable.
umount -l /oldroot
rmdir /oldroot 2>/dev/null || true

echo "pivoted. This is now / :"
ls /
echo "mounts visible here:"
cat /proc/mounts 2>/dev/null | wc -l || echo "(no /proc)"
exec /bin/sh
