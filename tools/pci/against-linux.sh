#!/usr/bin/env bash
# Compares what the kernel reads of each PCI function through ECAM with what Linux reads of it, on the same QEMU q35.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GUEST="$ROOT/build/pci-guest"

usage() {
    cat <<'USAGE'
Usage: tools/pci/against-linux.sh fetch
       tools/pci/against-linux.sh run <kernel> [<QEMU device option>...]

  fetch  downloads the current Ubuntu generic kernel and a static busybox with apt-get download (no root), and
         builds in build/pci-guest/ an initramfs whose init prints the configuration space of every PCI function,
         as /sys/bus/pci/devices/*/config gives it, then powers off
  run    boots that guest, then <kernel>, a debug LplKernel, on qemu-system-x86_64 and qemu-system-i386 -machine q35
         with the same device options, for example `-device pcie-root-port,id=port,chassis=1 -device e1000e,bus=port`;
         then tools/pci/against_linux.py compares, for each function Linux lists, the kernel's `[LPLTLM]
         pci.function` record with Linux's bytes: the same function, PCI Express exactly where Linux reads 4 KiB, and
         the extended capabilities and the end of their list that a walk of Linux's bytes gives; lspci -vvv, when it
         is installed, decodes the same bytes and must name each capability at the same offset and version

Exit status: 0 when every function agrees, 1 when one does not, 2 on a usage error or a missing guest.
USAGE
}

fetch() {
    local work version
    work="$(mktemp -d)"
    version="$(apt-cache depends linux-image-generic | grep -o 'linux-image-[0-9][^ ]*-generic' | head -1)"
    [[ -n "$version" ]] || { echo "fetch: apt names no linux-image-generic" >&2; exit 2; }
    (cd "$work" && apt-get download -q "$version" busybox-static >/dev/null)
    rm -rf "$GUEST" && mkdir -p "$GUEST/image" "$GUEST/busybox" "$GUEST/root/bin" "$GUEST/root/proc" "$GUEST/root/sys"
    dpkg -x "$work"/linux-image-*.deb "$GUEST/image"
    dpkg -x "$work"/busybox-static*.deb "$GUEST/busybox"
    rm -rf "$work"
    cp "$GUEST/busybox/usr/bin/busybox" "$GUEST/root/bin/busybox"
    cat >"$GUEST/root/init" <<'INIT'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sys /sys
for device in /sys/bus/pci/devices/*; do
    echo "CONFIG ${device##*/} $(wc -c < "$device/config")"
    od -A x -t x1 -v "$device/config"
done
echo GUEST-DONE
poweroff -f
INIT
    chmod +x "$GUEST/root/init"
    (cd "$GUEST/root" && find . | "$GUEST/root/bin/busybox" cpio -o -H newc >"$GUEST/initramfs.cpio" 2>/dev/null)
    ls "$GUEST"/image/boot/vmlinuz-* | head -1 >"$GUEST/kernel"
    echo "fetch: $version and busybox-static in $GUEST"
}

run() {
    local kernel="$1" guest_log kernel_log qemu_pid
    shift
    [[ -f "$GUEST/kernel" && -f "$GUEST/initramfs.cpio" ]] || { echo "run: no guest, run fetch first" >&2; exit 2; }
    [[ -f "$kernel" ]] || { echo "run: $kernel: no such kernel" >&2; exit 2; }
    guest_log="$(mktemp)" kernel_log="$(mktemp)"
    env -u LD_LIBRARY_PATH timeout 600 qemu-system-x86_64 -machine q35 -m 1G -smp 2 -nographic -no-reboot \
        -kernel "$(cat "$GUEST/kernel")" -initrd "$GUEST/initramfs.cpio" -append "console=ttyS0 panic=-1 loglevel=1" \
        "$@" >"$guest_log" 2>&1 || true
    grep -q GUEST-DONE "$guest_log" || { echo "run: the guest did not finish"; tail -5 "$guest_log"; exit 1; }
    env -u LD_LIBRARY_PATH timeout 900 qemu-system-i386 -machine q35 -m 1G -smp 2 -display none -no-reboot \
        -monitor none -serial file:"$kernel_log" -kernel "$kernel" -append "lpl.test=none" "$@" &
    qemu_pid=$!
    until grep -q '^# Totals:' "$kernel_log" 2>/dev/null || ! kill -0 "$qemu_pid" 2>/dev/null; do sleep 1; done
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
    python3 -I "$ROOT/tools/pci/against_linux.py" "$guest_log" "$kernel_log"
    local status=$?
    rm -f "$guest_log" "$kernel_log"
    return "$status"
}

case "${1:-}" in
    fetch) fetch ;;
    run) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; shift; run "$@" ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
