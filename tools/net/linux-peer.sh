#!/usr/bin/env bash
set -u

usage() {
    cat <<'USAGE'
Usage: tools/net/linux-peer.sh <debug kernel>

Boots <kernel> on QEMU with an Intel 82540EM on a tap device of an unprivileged network namespace
(`unshare --user --map-root-user --net`), whose own Linux stack is the peer: the tap at 10.0.0.1/24, the
kernel at 10.0.0.2. The kernel runs network_interface.answers_a_linux_peer while the peer pings it five
times, then sends five UDP datagrams from 10.0.0.1:7777 to its port 9000, which the kernel echoes; the peer
then prints the station address it resolved for 10.0.0.2 by ARP.

No root is needed; the namespace and its tap go away with the script. LOG names the serial log, kept;
without it the log is a temporary file, removed on exit. BOOT_TIMEOUT is the seconds the boot may take (180).

Exit status: 0 when the peer got its five echo replies and its five datagrams back byte for byte, the FNV-1a
of the payloads the kernel received is the one of the payloads the peer sent, the peer resolved the address to
the kernel's e1000, and the kernel's test passed; 1 when one of these failed; 2 for a wrong use, a missing
kernel, or a namespace that could not make its tap device.
USAGE
}

[ "${1:-}" = "--help" ] && { usage; exit 0; }
[ $# -eq 1 ] || { usage >&2; exit 2; }
kernel=$1
[ -f "$kernel" ] || { echo "linux-peer: no kernel at $kernel" >&2; exit 2; }
if [ -n "${LOG:-}" ]; then
    log=$LOG
else
    log=$(mktemp)
    trap 'rm -f "$log"' EXIT
fi
timeout_seconds=${BOOT_TIMEOUT:-180}

unshare --user --map-root-user --net bash -s "$kernel" "$log" "$timeout_seconds" <<'NAMESPACE'
set -u
kernel=$1 log=$2 timeout_seconds=$3
test_name=network_interface.answers_a_linux_peer
ip tuntap add dev tap0 mode tap && ip addr add 10.0.0.1/24 dev tap0 && ip link set tap0 up \
    || { echo "linux-peer: the namespace could not make its tap device" >&2; exit 2; }
rm -f "$log"
qemu-system-i386 -kernel "$kernel" -append "lpl.test=$test_name" -m 256M -smp 2 -display none -no-reboot \
    -serial "file:$log" -netdev tap,id=lplpeer,ifname=tap0,script=no,downscript=no \
    -device e1000,netdev=lplpeer >/dev/null 2>&1 &
qemu=$!
waited=0
until grep -q "answering at 10.0.0.2" "$log" 2>/dev/null; do
    kill -0 "$qemu" 2>/dev/null || { echo "linux-peer: QEMU stopped before the test began" >&2; exit 1; }
    [ "$waited" -lt "$((timeout_seconds * 2))" ] || { echo "linux-peer: the test never began" >&2; kill "$qemu"; exit 1; }
    sleep 0.5
    waited=$((waited + 1))
done
ping_report=$(ping -c 5 -i 0.5 -W 2 10.0.0.2)
echo "$ping_report"
udp_report=$(python3 -I - <<'DATAGRAMS'
import hashlib
import socket

payloads = [(hashlib.sha256(bytes([index])).digest() * 2)[: 7 + 13 * index] for index in range(5)]
peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
peer.bind(("10.0.0.1", 7777))
peer.settimeout(3)
echoed = 0
for payload in payloads:
    peer.sendto(payload, ("10.0.0.2", 9000))
    try:
        data, sender = peer.recvfrom(2048)
    except socket.timeout:
        continue
    echoed += data == payload and sender == ("10.0.0.2", 9000)
fold = 0x811C9DC5
for payload in payloads:
    for byte in payload:
        fold = ((fold ^ byte) * 0x01000193) & 0xFFFFFFFF
print(f"udp: {echoed} of {len(payloads)} echoed, payloads_fnv=0x{fold:08X}")
DATAGRAMS
)
echo "linux-peer: $udp_report"
neighbour=$(ip neigh show 10.0.0.2 dev tap0)
echo "linux-peer: neighbour $neighbour"
waited=0
until grep -q "# Totals:" "$log" 2>/dev/null; do
    kill -0 "$qemu" 2>/dev/null || break
    [ "$waited" -lt "$((timeout_seconds * 2))" ] || break
    sleep 0.5
    waited=$((waited + 1))
done
kill "$qemu" 2>/dev/null
wait "$qemu" 2>/dev/null
grep -E "answers_a_linux_peer|# Totals:" "$log"
status=0
echo "$udp_report" | grep -q "udp: 5 of 5 echoed" \
    || { echo "linux-peer: the peer did not get its five datagrams back byte for byte" >&2; status=1; }
peer_fold=$(echo "$udp_report" | grep -oE "payloads_fnv=0x[0-9A-F]{8}")
grep -qE "answers_a_linux_peer: ${peer_fold:-payloads_fnv=none}\$" "$log" \
    || { echo "linux-peer: the kernel's FNV-1a of the payloads it received is not the peer's ${peer_fold:-}" >&2
         status=1; }
echo "$ping_report" | grep -q "5 packets transmitted, 5 received" \
    || { echo "linux-peer: the peer did not get its five echo replies" >&2; status=1; }
echo "$neighbour" | grep -q "lladdr 52:54:00:12:34:56" \
    || { echo "linux-peer: the peer did not resolve 10.0.0.2 to the e1000's 52:54:00:12:34:56" >&2; status=1; }
grep -qE "^ +ok [0-9]+ answers_a_linux_peer$" "$log" || { echo "linux-peer: the kernel's test did not pass" >&2; status=1; }
exit "$status"
NAMESPACE
