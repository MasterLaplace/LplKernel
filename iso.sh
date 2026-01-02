#!/bin/sh
set -e
. ./build.sh "$@"

mkdir -p iso/boot/grub

cp sysroot/boot/lpl.kernel iso/boot/lpl.kernel

if [ "$GRAPHICS_MODE" = "1" ]; then
    cat > iso/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

# Graphics mode configuration
insmod all_video
insmod vbe
insmod gfxterm

# Set graphics mode BEFORE menu appears
set gfxmode=1024x768x32,800x600x32,auto
terminal_output gfxterm

menuentry "lpl" {
    # Force graphics payload to kernel
    set gfxpayload=keep
    multiboot /boot/lpl.kernel
    boot
}
EOF
    GRUB_MODULES="multiboot all_video vbe gfxterm"
else
    cat > iso/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "lpl" {
    multiboot /boot/lpl.kernel
    boot
}
EOF
    GRUB_MODULES="multiboot"
fi

grub-mkrescue -o lpl.iso iso --install-modules="" \
  --modules="$GRUB_MODULES" \
  --fonts="" --themes="" --locales=""
