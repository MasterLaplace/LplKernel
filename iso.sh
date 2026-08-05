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
#
# -P, and not `cd` into the submodule. The submodule is a NESTED xmake project
# inside LplKernel's own, so `cd LplPlugin && xmake build lpl-bake` resolves the
# PARENT project and writes its output to ./build at the root — while the search
# below used to look only in LplPlugin/build, and therefore kept finding a binary
# from a previous life. That is exactly the silent failure the comment above warns
# about, and it came back: the ISO shipped a lpl-bake four days stale, which wrote
# a pack missing the section that had just been added, and nothing said a word.
if command -v xmake >/dev/null 2>&1 && [ -f "${LPLPLUGIN_ROOT:-LplPlugin}/xmake.lua" ]; then
    BAKER_LOG="$(mktemp)"
    if ! env -u CC -u CXX -u AR -u AS -u LD -u RANLIB \
              -u CFLAGS -u CXXFLAGS -u LDFLAGS -u ASFLAGS -u SYSROOT -u DESTDIR \
              xmake build -P "${LPLPLUGIN_ROOT:-LplPlugin}" -y lpl-bake >"$BAKER_LOG" 2>&1; then
        echo "[iso] warning: could not rebuild lpl-bake, using whatever is present"
        # The failure is PRINTED. Swallowing it is how a stale baker survives.
        tail -20 "$BAKER_LOG" | sed 's/^/[iso]   /'
    fi
    rm -f "$BAKER_LOG"
fi

# Whichever candidate is NEWEST wins, not whichever is listed first. Two build
# trees can hold a lpl-bake (the root project's and the submodule's own), and
# "first in the list" silently prefers one of them forever.
CARTRIDGE_BAKER="$(command -v lpl-bake || true)"
for candidate in build/*/*/debug/lpl-bake build/*/*/release/lpl-bake \
                 "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/debug/lpl-bake \
                 "${LPLPLUGIN_ROOT:-LplPlugin}"/build/*/*/release/lpl-bake; do
    [ -x "$candidate" ] || continue
    if [ -z "$CARTRIDGE_BAKER" ] || [ "$candidate" -nt "$CARTRIDGE_BAKER" ]; then
        CARTRIDGE_BAKER="$candidate"
    fi
done
[ -n "$CARTRIDGE_BAKER" ] && echo "[iso] baker: $CARTRIDGE_BAKER"

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
# --ecc on the viewer cartridge and NOT on the parity one, deliberately.
#
# The viewer's world is the one that gets stored, shipped and read off a disc, so it
# is the one a bad sector can reach: it gets a transversal Reed-Solomon section and
# the ring-0 reader repairs it instead of falling back. The parity cartridge is the
# gate's own reference, compared byte for byte against a freshly baked oracle — adding
# bytes to it would only mean comparing a different image.
if [ -n "$CARTRIDGE_BAKER" ] && [ -f "$VIEWER_SCENE" ]; then
    "$CARTRIDGE_BAKER" --ecc "$VIEWER_SCENE" iso/boot/world.lplpak
    echo "[iso] cartridge (world viewer, parity attached): iso/boot/world.lplpak"
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
