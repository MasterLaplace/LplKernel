#!/bin/sh
set -eu

if [ "${1:-}" = "" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS="1"
    fi
else
    JOBS="$1"
fi

require_file() {
    path="$1"
    if [ ! -f "$path" ]; then
        echo "[pipeline][error] Missing required file: $path" >&2
        exit 1
    fi
}

build_profile() {
    profile="$1"
    iso_name="$2"
    kernel_name="$3"

    echo "[pipeline] Building profile: ${profile} (jobs=${JOBS})"
    ./iso.sh "--${profile}" "${JOBS}"

    require_file lpl.iso
    require_file sysroot/boot/lpl.kernel

    cp lpl.iso "${iso_name}"
    cp sysroot/boot/lpl.kernel "${kernel_name}"

    require_file "${iso_name}"
    require_file "${kernel_name}"
    echo "[pipeline] Produced ${iso_name} and ${kernel_name}"
}

./clean.sh
build_profile client lpl-client.iso lpl-client.kernel
build_profile server lpl-server.iso lpl-server.kernel

echo "[pipeline] Artifact summary:"
ls -lh lpl-client.iso lpl-server.iso lpl-client.kernel lpl-server.kernel

echo "[pipeline] Dual-profile build pipeline completed successfully"
