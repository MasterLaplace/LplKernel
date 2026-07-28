#!/bin/sh
set -e
. ./build.sh "$@"

mkdir -p iso/boot/grub


cp sysroot/boot/lpl.kernel iso/boot/lpl.kernel

# The cartridge. GRUB loads it as a multiboot module, so the kernel receives a
# game as bytes without needing a filesystem — a console loads a cartridge, it
# does not mount a disk. Baked from the authored .lplscene when the host tool is
# available; an ISO without one still boots on the built-in reference pack.
CARTRIDGE_SCENE="${LPLPLUGIN_ROOT:-LplPlugin}/assets/games/parity.lplscene"
CARTRIDGE_BAKER="$(command -v lpl-bake || true)"
if [ -z "$CARTRIDGE_BAKER" ]; then
    for candidate in "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/debug/lpl-bake \
                     "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/release/lpl-bake; do
        [ -x "$candidate" ] && CARTRIDGE_BAKER="$candidate" && break
    done
fi

rm -f iso/boot/game.lplpak
if [ -n "$CARTRIDGE_BAKER" ] && [ -f "$CARTRIDGE_SCENE" ]; then
    "$CARTRIDGE_BAKER" "$CARTRIDGE_SCENE" iso/boot/game.lplpak
    echo "[iso] cartridge: iso/boot/game.lplpak"
else
    echo "[iso] no cartridge baked (lpl-bake or $CARTRIDGE_SCENE missing) — kernel will use its built-in pack"
fi

# GRUB needs the module line only when a cartridge exists; an empty variable
# leaves a blank line in the menu entry, which grub.cfg tolerates.
if [ -f iso/boot/game.lplpak ]; then
    CARTRIDGE_ENTRY="    module /boot/game.lplpak game.lplpak"
else
    CARTRIDGE_ENTRY=""
fi

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
${CARTRIDGE_ENTRY}
    boot
}
EOF
    GRUB_MODULES="multiboot all_video gfxterm"
else
    cat > iso/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "lpl" {
    multiboot /boot/lpl.kernel
${CARTRIDGE_ENTRY}
    boot
}
EOF
    GRUB_MODULES="multiboot"
fi

grub-mkrescue -o lpl.iso iso --install-modules="" \
  --modules="$GRUB_MODULES" \
  --fonts="" --themes="" --locales=""
