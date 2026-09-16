#!/usr/bin/env bash
# setup-bridge.sh — build a miniature "docker0" by hand.
#
#   host namespace:  br0 10.200.0.1/24, IP forwarding, masquerade for 10.200.0.0/24
#   namespace a:     eth0 10.200.0.2/24, default route via 10.200.0.1
#   namespace b:     eth0 10.200.0.3/24, default route via 10.200.0.1
#
# Usage: sudo ./setup-bridge.sh      (undo with sudo ./teardown.sh)
set -euo pipefail
set -x   # print every command: each one is a netlink or nftables operation to study

ip link add br0 type bridge
ip addr add 10.200.0.1/24 dev br0
ip link set br0 up

for spec in a:10.200.0.2 b:10.200.0.3; do
    ns=${spec%%:*}; addr=${spec##*:}
    ip netns add "$ns"
    ip link add "veth-$ns" type veth peer name "eth0-$ns"   # both ends start in the host ns
    ip link set "eth0-$ns" netns "$ns"                       # move one end into the namespace
    ip -n "$ns" link set "eth0-$ns" name eth0                # rename it inside
    ip link set "veth-$ns" master br0                        # plug the host end into the bridge
    ip link set "veth-$ns" up
    ip -n "$ns" addr add "$addr/24" dev eth0
    ip -n "$ns" link set lo up
    ip -n "$ns" link set eth0 up
    ip -n "$ns" route add default via 10.200.0.1
done

# Remember the previous forwarding setting so teardown.sh can restore it.
cat /proc/sys/net/ipv4/ip_forward > /run/netns-lab-ip_forward
sysctl -w net.ipv4.ip_forward=1

nft add table ip labnat
nft add chain ip labnat postrouting '{ type nat hook postrouting priority 100 ; }'
nft add rule ip labnat postrouting ip saddr 10.200.0.0/24 oifname != "br0" masquerade
