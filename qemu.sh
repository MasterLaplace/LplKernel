#!/bin/sh
set -e
. ./iso.sh "$@"

# Use -vga std for better VBE/graphics mode support
QEMU_VGA_OPT=""
if [ "$GRAPHICS_MODE" = "1" ]; then
    QEMU_VGA_OPT="-vga std"
fi

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom lpl.iso -m 256M -serial stdio $QEMU_VGA_OPT
