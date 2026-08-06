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

# The pointer, and it needs saying because nothing about it is visible from inside.
#
# QEMU's default display is GTK, and GTK forwards RELATIVE pointer motion only while
# the pointer is GRABBED. The kernel has a PS/2 mouse and nothing else, and PS/2 is a
# relative device — so with no grab the guest sees no motion at all, however far the
# trackpad moves, and "the mouse does not work" is indistinguishable from a driver
# that never fires.
#
# Measured from the other end before touching this: motion injected through the QEMU
# monitor reaches the game and turns the view, and the kernel logs "pointer: motion
# received — the device and the driver are fine". So the driver, the interrupt and the
# consumer were never the problem; the window was.
#
# grab-on-hover captures as soon as the pointer is over the window, which is what a
# trackpad wants: no click to focus, no Ctrl+Alt+G to remember. Ctrl+Alt+G still
# RELEASES it, which is how you get your cursor back.
#
# Probed rather than assumed: a QEMU without the suboption launches exactly as before
# instead of refusing to start.
QEMU_DISPLAY_OPT=""
if [ "$GRAPHICS_MODE" = "1" ]; then
    if "$QEMU_CMD" -help 2>/dev/null | grep -q 'grab-on-hover'; then
        QEMU_DISPLAY_OPT="-display gtk,grab-on-hover=on"
        echo "[qemu] pointer grabbed on hover — mouse look is live; Ctrl+Alt+G releases it"
    else
        echo "[qemu] this QEMU has no grab-on-hover: click in the window or press Ctrl+Alt+G to look around"
    fi
fi

# Clear out any sandbox/snap environment that might pollute the loader search path.
# this mirrors what `sudo` does and prevents qemu from trying to load
# libc from /snap/core20/current.
unset LD_LIBRARY_PATH LD_PRELOAD GTK_PATH XDG_DATA_DIRS
# also clear any SNAP variables individually if they exist
unset SNAP SNAP_VERSION SNAP_ARCH SNAP_REVISION
# execute with a minimal PATH as well
PATH="/usr/bin:/bin" \
    "$QEMU_CMD" -cdrom lpl.iso -m 256M -serial stdio $QEMU_VGA_OPT $QEMU_DISPLAY_OPT $QEMU_ACCEL_OPT
