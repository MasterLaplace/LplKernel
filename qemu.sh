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

# Clear out any sandbox/snap environment that might pollute the loader search path.
# this mirrors what `sudo` does and prevents qemu from trying to load
# libc from /snap/core20/current.
unset LD_LIBRARY_PATH LD_PRELOAD GTK_PATH XDG_DATA_DIRS
# also clear any SNAP variables individually if they exist
unset SNAP SNAP_VERSION SNAP_ARCH SNAP_REVISION
# execute with a minimal PATH as well
PATH="/usr/bin:/bin" \
    "$QEMU_CMD" -cdrom lpl.iso -m 256M -serial stdio $QEMU_VGA_OPT
