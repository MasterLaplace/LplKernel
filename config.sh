SYSTEM_HEADER_PROJECTS="libc libkxx libengine kernel"
PROJECTS="libc libkxx libengine kernel"

export MAKE=${MAKE:-make}
export HOST=${HOST:-$(./default-host.sh)}

export AR=${HOST}-ar
export AS=${HOST}-as
export CC=${HOST}-gcc
export CXX=${HOST}-g++

# Root of the LplPlugin source tree (single source of truth). Defaults to a
# git submodule checkout under the kernel repo; falls back to a sibling
# working checkout for local development. Overridable from the environment.
if [ -z "${LPLPLUGIN_ROOT:-}" ]; then
    if [ -d "$(pwd)/LplPlugin/core/include" ]; then
        export LPLPLUGIN_ROOT="$(pwd)/LplPlugin"
    else
        export LPLPLUGIN_ROOT="$(cd "$(pwd)/../LplPlugin" 2>/dev/null && pwd)"
    fi
fi

# The engine module is optional: when the LplPlugin source tree is absent, build
# a plain kernel (no libengine, smoke battery compiled out) instead of failing.
# This is the "no xmake / no LplPlugin -> fallback kernel" path.
if [ -n "${LPLPLUGIN_ROOT:-}" ] && [ -d "${LPLPLUGIN_ROOT}/core/include" ]; then
    export ENABLE_LIBENGINE=1
else
    export ENABLE_LIBENGINE=0
    SYSTEM_HEADER_PROJECTS="libc libkxx kernel"
    PROJECTS="libc libkxx kernel"
    echo "[config] LplPlugin not found -> building a plain kernel (ENABLE_LIBENGINE=0)"
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
