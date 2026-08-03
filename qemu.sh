#!/bin/sh
set -e
. ./iso.sh "$@"

# Use -vga std for better VBE/graphics mode support
QEMU_VGA_OPT=""
if [ "$GRAPHICS_MODE" = "1" ]; then
    QEMU_VGA_OPT="-vga std"
fi

# pick the canonical executable (avoid snap shims that leak libs)
QEMU_CMD=$(command -v qemu-system-$(./target-triplet-to-arch.sh $HOST) || true)
# if the path points into /snap assume the real system one is in /usr/bin
if [ -n "$QEMU_CMD" ] && [ "$(echo "$QEMU_CMD" | grep -q '^/snap/' && echo yes)" = yes ]; then
    QEMU_CMD="/usr/bin/$(basename "$QEMU_CMD")"
fi
# fall back to the unqualified name if we have nothing
QEMU_CMD=${QEMU_CMD:-qemu-system-$(./target-triplet-to-arch.sh $HOST)}

# Hardware acceleration when the machine can give it, plain interpretation when it
# cannot.
#
# This is the single biggest number on the whole demo and it was never asked for.
# Without -accel, QEMU falls back to TCG: every x86 instruction the kernel executes
# is DECODED AND INTERPRETED on the host. A software rasterizer is exactly the
# workload that punishes hardest — a tight arithmetic loop over hundreds of
# thousands of pixels, none of which the emulator can skip.
#
# The measurement that pointed here: the triangle count was moved from 112 000 to
# 23 000 to 4 500 across three builds and the frame rate did not shift once, staying
# at six frames per hundred ticks throughout. A renderer whose cost does not depend
# on how much it draws is not the thing being measured — the interpreter underneath
# it is.
#
# Probed rather than assumed, and never fatal: a host without /dev/kvm, or a user
# who is not in the kvm group, boots exactly as before instead of failing.
QEMU_ACCEL_OPT=""
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    QEMU_ACCEL_OPT="-accel kvm -cpu host"
    echo "[qemu] KVM available: running with hardware acceleration"
else
    echo "[qemu] no usable /dev/kvm — falling back to TCG (interpreted, much slower)"
    echo "[qemu] to enable it:  sudo usermod -aG kvm \$USER   then log out and back in"
fi

# Clear out any sandbox/snap environment that might pollute the loader search path.
# this mirrors what `sudo` does and prevents qemu from trying to load
# libc from /snap/core20/current.
unset LD_LIBRARY_PATH LD_PRELOAD GTK_PATH XDG_DATA_DIRS
# also clear any SNAP variables individually if they exist
unset SNAP SNAP_VERSION SNAP_ARCH SNAP_REVISION
# execute with a minimal PATH as well
PATH="/usr/bin:/bin" \
    "$QEMU_CMD" -cdrom lpl.iso -m 256M -serial stdio $QEMU_VGA_OPT $QEMU_ACCEL_OPT
