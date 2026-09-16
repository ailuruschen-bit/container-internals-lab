#!/usr/bin/env bash
# teardown.sh — remove everything created by this lab. Safe to run repeatedly.
set -uo pipefail

ip netns delete a 2>/dev/null        # destroying a namespace destroys its veth end,
ip netns delete b 2>/dev/null        # which also destroys the peer end in the host
ip netns delete blue 2>/dev/null
ip link delete veth-host 2>/dev/null
ip link delete br0 2>/dev/null
nft delete table ip labnat 2>/dev/null

if [[ -f /run/netns-lab-ip_forward ]]; then
    sysctl -w net.ipv4.ip_forward="$(cat /run/netns-lab-ip_forward)"
    rm -f /run/netns-lab-ip_forward
fi

ip netns list
ip -brief link | grep -E 'br0|veth' || echo "no lab interfaces left"
