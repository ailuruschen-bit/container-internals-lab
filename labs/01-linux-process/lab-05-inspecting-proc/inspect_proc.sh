#!/usr/bin/env bash
# inspect_proc.sh — summarize kernel process state using only /proc.
#
# Usage: ./inspect_proc.sh [PID]      (default: the shell you run the script from)
#        sudo ./inspect_proc.sh PID   (needed for processes of other users)
#
# Every value printed below is read from a file under /proc/<pid>/.
# Later chapters reuse this script to observe namespaces, cgroups,
# capabilities, and seccomp.
set -euo pipefail

pid="${1:-$PPID}"
p="/proc/$pid"
[[ -d "$p" ]] || { echo "no such process: $pid" >&2; exit 1; }

field() { awk -v k="$1:" '$1 == k { $1 = ""; sub(/^ +/, ""); print }' "$p/status"; }
link()  { readlink "$p/$1" 2>/dev/null || echo "<permission denied>"; }

echo "== identity ($p/status)"
printf '  %-10s %s\n' Name "$(field Name)" State "$(field State)" \
    Pid "$(field Pid)" Tgid "$(field Tgid)" PPid "$(field PPid)" Threads "$(field Threads)"

echo "== program"
printf '  %-10s %s\n' exe "$(link exe)"
printf '  %-10s ' cmdline; tr '\0' ' ' < "$p/cmdline"; echo

echo "== filesystem context"
printf '  %-10s %s\n' cwd "$(link cwd)" root "$(link root)"

echo "== credentials (real effective saved filesystem)"
printf '  %-10s %s\n' Uid "$(field Uid)" Gid "$(field Gid)" Groups "$(field Groups)"

echo "== open file descriptors"
if ls "$p/fd" >/dev/null 2>&1; then
    for fd in "$p"/fd/*; do printf '  %-4s -> %s\n' "${fd##*/}" "$(readlink "$fd" || echo '<closed>')"; done
else
    echo "  <permission denied>"
fi

echo "== namespaces (Chapter 03)"
if ls "$p/ns" >/dev/null 2>&1; then
    for ns in "$p"/ns/*; do printf '  %-18s %s\n' "${ns##*/}" "$(readlink "$ns" 2>/dev/null || echo '<permission denied>')"; done
else
    echo "  <permission denied>"
fi

echo "== cgroup (Chapter 04)"
sed 's/^/  /' "$p/cgroup"

echo "== security (Chapters 05-06)"
printf '  %-10s %s\n' CapEff "$(field CapEff)" CapBnd "$(field CapBnd)" \
    NoNewPrivs "$(field NoNewPrivs)" Seccomp "$(field Seccomp)"
