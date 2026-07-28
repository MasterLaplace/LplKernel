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
# Build the baker from THIS tree before using it.
#
# Without this the ISO takes whichever lpl-bake happens to be lying in the build
# directory, and a stale one is not a loud failure: it writes a structurally valid
# pack whose recipe section is the size the OLD RecipeV1 had. The kernel then
# rejects it correctly (P7 reports pack_ok=0) and the viewer falls back to its
# compiled recipe, so the ISO boots, shows a world, and quietly ignores the
# cartridge it is carrying. The tool that writes the cartridge has to come from
# the same tree as the kernel that reads it.
#
# A subshell that CHANGES DIRECTORY, and with the cross-compiler variables
# cleared: xmake resolves its configuration from the working directory, and this
# script runs with CC/CXX pointing at i686-elf — so building a HOST tool from
# here otherwise hands it the kernel's own toolchain and fails with "cannot find
# known tool script for i686-elf-g++".
if command -v xmake >/dev/null 2>&1 && [ -f "${LPLPLUGIN_ROOT:-LplPlugin}/xmake.lua" ]; then
    if ! (cd "${LPLPLUGIN_ROOT:-LplPlugin}" && env -u CC -u CXX -u AR -u AS -u LD -u RANLIB \
              -u CFLAGS -u CXXFLAGS -u LDFLAGS -u ASFLAGS -u SYSROOT -u DESTDIR \
              xmake build lpl-bake) >/dev/null 2>&1; then
        echo "[iso] warning: could not rebuild lpl-bake, using whatever is present"
    fi
fi

CARTRIDGE_BAKER="$(command -v lpl-bake || true)"
if [ -z "$CARTRIDGE_BAKER" ]; then
    for candidate in "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/debug/lpl-bake \
                     "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/release/lpl-bake; do
        [ -x "$candidate" ] && CARTRIDGE_BAKER="$candidate" && break
    done
fi

# The world the graphical profile DRAWS, which is a different question from the
# world the parity gate FOLDS. Two cartridges rather than one: the gate boots
# game.lplpak and compares the fold against an oracle that baked that same scene,
# so handing it a richer world instead would compare the kernel against something
# nobody baked. The viewer takes world.lplpak and ignores the other.
VIEWER_SCENE="${LPLPLUGIN_ROOT:-LplPlugin}/assets/games/worldview.lplscene"

rm -f iso/boot/game.lplpak iso/boot/world.lplpak
if [ -n "$CARTRIDGE_BAKER" ] && [ -f "$CARTRIDGE_SCENE" ]; then
    "$CARTRIDGE_BAKER" "$CARTRIDGE_SCENE" iso/boot/game.lplpak
    echo "[iso] cartridge (parity gate): iso/boot/game.lplpak"
else
    echo "[iso] no cartridge baked (lpl-bake or $CARTRIDGE_SCENE missing) — kernel will use its built-in pack"
fi
if [ -n "$CARTRIDGE_BAKER" ] && [ -f "$VIEWER_SCENE" ]; then
    "$CARTRIDGE_BAKER" "$VIEWER_SCENE" iso/boot/world.lplpak
    echo "[iso] cartridge (world viewer): iso/boot/world.lplpak"
fi

# GRUB needs the module line only when a cartridge exists; an empty variable
# leaves a blank line in the menu entry, which grub.cfg tolerates.
CARTRIDGE_ENTRY=""
if [ -f iso/boot/game.lplpak ]; then
    CARTRIDGE_ENTRY="    module /boot/game.lplpak game.lplpak"
fi
if [ -f iso/boot/world.lplpak ]; then
    CARTRIDGE_ENTRY="${CARTRIDGE_ENTRY}
    module /boot/world.lplpak world.lplpak"
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
