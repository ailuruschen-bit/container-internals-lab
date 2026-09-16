#!/usr/bin/env bash
# build-busybox-rootfs.sh — create a tiny, complete rootfs from BusyBox.
#
# Usage: ./build-busybox-rootfs.sh [DEST]     (default: /tmp/rootfs)
#
# The result is a directory tree that can serve as a container's /.
set -euo pipefail
DEST="${1:-/tmp/rootfs}"

BB="$(command -v busybox || true)"
if [[ -z "$BB" ]]; then
    echo "busybox not found. Install it: sudo apt-get install -y busybox-static" >&2
    exit 1
fi

echo "building rootfs at $DEST using $BB"
rm -rf "$DEST"
mkdir -p "$DEST"/{bin,sbin,etc,proc,sys,dev,tmp,root,usr/bin,usr/sbin}
chmod 1777 "$DEST/tmp"

cp "$BB" "$DEST/bin/busybox"
# Create symlinks (ls, sh, cat, ...) pointing at busybox, inside the rootfs.
"$DEST/bin/busybox" --list | while read -r applet; do
    ln -sf busybox "$DEST/bin/$applet" 2>/dev/null || true
done

# Is this busybox static? If not, copy its libraries too.
if ldd "$BB" 2>/dev/null | grep -q '=>'; then
    echo "busybox is dynamically linked; copying its libraries"
    mkdir -p "$DEST/lib" "$DEST/lib64"
    ldd "$BB" | awk '/=>/{print $3} /ld-linux|ld-musl/{print $1}' | sort -u | while read -r lib; do
        [[ -f "$lib" ]] || continue
        mkdir -p "$DEST$(dirname "$lib")"
        cp -L "$lib" "$DEST$lib"
    done
else
    echo "busybox is static; no libraries needed"
fi

# Minimal /etc so that name lookups and prompts work.
cat > "$DEST/etc/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/sh
EOF
cat > "$DEST/etc/group" <<'EOF'
root:x:0:
EOF
echo 'container' > "$DEST/etc/hostname"

echo "done. Tree:"
find "$DEST" -maxdepth 2 | head -n 30
echo "size: $(du -sh "$DEST" | cut -f1)"
