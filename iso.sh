#!/bin/sh
set -e
. ./build.sh "$@"

mkdir -p iso/boot/grub

cp sysroot/boot/lpl.kernel iso/boot/lpl.kernel
cat > iso/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "lpl" {
    multiboot /boot/lpl.kernel
    boot
}
EOF
grub-mkrescue -o lpl.iso iso --install-modules="" \
  --modules="multiboot" \
  --fonts="" --themes="" --locales=""
