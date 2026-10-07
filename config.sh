# libassistant and libknowledge are both here, inserted conditionally below: each needs
# its own sibling checkout for sources and libengine for the foundation it is written
# against (Fixed32, CORDIC, the one arena implementation, and for libknowledge also
# `lpl::history` — the arithmetic of doubt it consumes and does not reimplement). Neither
# can outlive libengine.
#
# ⚠ The rule that put libknowledge on this list is the rule that had kept it off: a build
# list is a list of things that BUILD. It sat out while its Makefile was the comment-only
# stub of lot 6 — no `all`, no `clean` — because a project listed here without a `clean`
# rule kills `build.sh` before it compiles anything, which is how both shell build paths
# came to be red while xmake stayed green. It is added on the commit that gives it real
# targets, which is this one.
SYSTEM_HEADER_PROJECTS="libc libkxx libengine kernel"
PROJECTS="libc libkxx libengine kernel"

export MAKE=${MAKE:-make}
export HOST=${HOST:-$(./default-host.sh)}

export AR=${HOST}-ar
export AS=${HOST}-as
export CC=${HOST}-gcc
export CXX=${HOST}-g++

# ccache in front of the cross compiler when it is there, and silently not when it
# is not — the CI image has no ccache and must keep building.
#
# It earns its place on this project specifically: the full validation compiles the kernel
# THREE times per run (the server ISO, the client ISO, and the xmake path), and a
# re-run after a one-line edit recompiles all of LplPlugin for i686 three times over.
# Measured before it was wired: 97 s for the client build alone, out of a 411 s run.
#
# Prepended rather than exported as CCACHE_* so it survives the flag appends below:
# config.sh goes on to add --sysroot and -isystem to $CC, and `ccache gcc --sysroot=…`
# is exactly what ccache expects.
if [ -z "${LPL_NO_CCACHE:-}" ] && command -v ccache >/dev/null 2>&1; then
    export CC="ccache $CC"
    export CXX="ccache $CXX"
fi

# Root of the LplPlugin source tree: a sibling checkout, like LplAssistant and LplKnowledge,
# so each repository has one working tree. DEPENDENCIES.lock names the commit each sibling
# was tested at, and `tools/deps.sh status` says how far the siblings are from it.
# Overridable from the environment; LPLPLUGIN_ROOT=none builds without the engine.
if [ -z "${LPLPLUGIN_ROOT:-}" ]; then
    export LPLPLUGIN_ROOT="$(cd "$(pwd)/../LplPlugin" 2>/dev/null && pwd)"
fi

# The engine module is optional: when the LplPlugin source tree is absent, build
# a plain kernel (no libengine, smoke battery compiled out) instead of failing.
# This is the "no xmake / no LplPlugin -> fallback kernel" path.
#
# libkxx is dropped along with libengine, not just unlinked: without the engine
# the kernel is pure C, so the freestanding C++ runtime has no consumer. This
# keeps the fallback buildable on a C-only cross toolchain -- build_lplkernel.yml
# builds gcc 10 with `all-gcc all-target-libgcc` and NO libstdc++, so <cstddef>
# and friends do not exist there. Requiring C++ on that path would break it.
#
# A profile that carries no engine (build.sh --satellite) exports ENABLE_LIBENGINE=0
# before this runs, and that choice stands even when LplPlugin is right there.
if [ "${ENABLE_LIBENGINE:-}" != 0 ] && [ -n "${LPLPLUGIN_ROOT:-}" ] && [ -d "${LPLPLUGIN_ROOT}/core/include" ]; then
    export ENABLE_LIBENGINE=1
else
    [ "${ENABLE_LIBENGINE:-}" = 0 ] ||
        echo "[config] LplPlugin not found -> building a plain kernel (ENABLE_LIBENGINE=0)"
    export ENABLE_LIBENGINE=0
    SYSTEM_HEADER_PROJECTS="libc kernel"
    PROJECTS="libc kernel"
fi

# Root of the LplAssistant source tree (the forward pass). A sibling checkout, like the
# engine. Overridable from the environment; LPLASSISTANT_ROOT=none builds without it.
if [ -z "${LPLASSISTANT_ROOT:-}" ]; then
    export LPLASSISTANT_ROOT="$(cd "$(pwd)/../LplAssistant" 2>/dev/null && pwd)"
fi

# The mind is optional the same way the engine is, and for a stronger reason: it is
# built on the engine's foundation, so it can never be present when libengine is not.
# Absent -> the kernel boots without a demon and the P14 gate is compiled out, exactly
# as it boots without a world when LplPlugin is missing.
#
# libassistant is inserted BEFORE kernel so its archive exists when the image links,
# and after libengine because the link order is -lassistant -lengine -lkxx -lk: the
# mind calls the foundation, which calls the C++ runtime, which calls the libc.
if [ "$ENABLE_LIBENGINE" = "1" ] && [ -n "${LPLASSISTANT_ROOT:-}" ] &&
   [ -d "${LPLASSISTANT_ROOT}/infer/include" ]; then
    export ENABLE_LIBASSISTANT=1
    SYSTEM_HEADER_PROJECTS="libc libkxx libengine libassistant kernel"
    PROJECTS="libc libkxx libengine libassistant kernel"
else
    export ENABLE_LIBASSISTANT=0
    [ "$ENABLE_LIBENGINE" = "1" ] && config_reason="LplAssistant not found" || config_reason="no engine to build it on"
    echo "[config] $config_reason -> building without a mind (ENABLE_LIBASSISTANT=0)"
fi

# Root of the LplKnowledge source tree (the memory). A sibling checkout, like the engine
# and the mind. Overridable from the environment; LPLKNOWLEDGE_ROOT=none builds without it.
if [ -z "${LPLKNOWLEDGE_ROOT:-}" ]; then
    export LPLKNOWLEDGE_ROOT="$(cd "$(pwd)/../LplKnowledge" 2>/dev/null && pwd)"
fi

# The memory is optional the same way the mind is, and rests on the same foundation: it
# reads a corpus into `lpl::history`, which lives in libengine. Absent -> the kernel boots
# without a memory and the P18 gate is compiled out, exactly as it boots without a world
# when LplPlugin is missing.
#
# Inserted BEFORE libassistant, because the link order is
# -lknowledge -lassistant -lengine -lkxx -lk: the memory calls the arithmetic of doubt in
# libengine, the mind calls the foundation there too, and a static archive only satisfies
# references already pending when the linker reaches it.
if [ "$ENABLE_LIBENGINE" = "1" ] && [ -n "${LPLKNOWLEDGE_ROOT:-}" ] &&
   [ -d "${LPLKNOWLEDGE_ROOT}/knowledge/include" ]; then
    export ENABLE_LIBKNOWLEDGE=1
    if [ "$ENABLE_LIBASSISTANT" = "1" ]; then
        SYSTEM_HEADER_PROJECTS="libc libkxx libengine libassistant libknowledge kernel"
        PROJECTS="libc libkxx libengine libassistant libknowledge kernel"
    else
        SYSTEM_HEADER_PROJECTS="libc libkxx libengine libknowledge kernel"
        PROJECTS="libc libkxx libengine libknowledge kernel"
    fi
else
    export ENABLE_LIBKNOWLEDGE=0
    [ "$ENABLE_LIBENGINE" = "1" ] && config_reason="LplKnowledge not found" || config_reason="no engine to build it on"
    echo "[config] $config_reason -> building without a memory (ENABLE_LIBKNOWLEDGE=0)"
fi

export PREFIX=/usr
export EXEC_PREFIX=$PREFIX
export BOOTDIR=/boot
export LIBDIR=$EXEC_PREFIX/lib
export INCLUDEDIR=$PREFIX/include

export CFLAGS='-O2 -g -nostdinc'
export CPPFLAGS=''

# Graphics mode configuration (default: text mode)
export GRAPHICS_MODE=${GRAPHICS_MODE:-0}

# Configure the cross-compiler to use the desired system root.
export SYSROOT="$(pwd)/sysroot"
export CC="$CC --sysroot=$SYSROOT"
export CXX="$CXX --sysroot=$SYSROOT"

# Work around that the -elf gcc targets doesn't have a system include directory
# because it was configured with --without-headers rather than --with-sysroot.
if echo "$HOST" | grep -Eq -- '-elf($|-)'; then
    export CC="$CC -isystem=$INCLUDEDIR"
    export CXX="$CXX -isystem=$INCLUDEDIR"
fi
