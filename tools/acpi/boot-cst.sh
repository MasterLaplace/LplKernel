#!/usr/bin/env bash
set -u

usage() {
    cat <<'USAGE'
Usage: tools/acpi/boot-cst.sh <kernel> [<qemu accelerator argument>...]

Boots <kernel>, a debug or release image, on QEMU q35 (256 MiB, two processors) with tools/acpi/boot-cst.asl loaded
by -acpitable: a _CST for QEMU's processors, which QEMU gives none. Then checks the C-states the boot prints: the
`[LPLTLM] cstates` record names CPU slot 0 and publishes hints 0 to 3, and the `[LPLTLM] cstate` records are those
the source's documentation works out by hand, from _CST's evaluation to each hint's latency and residency. The
machine-dependent keys, `monitor`, `enumerated` and `usable`, are printed and not compared.

The table is compiled with the pinned iasl of tools/acpi/corpus.sh fetch acpica, in the sandbox of
tools/acpi/sandbox.sh, as tools/acpi/regenerate-fixtures.sh compiles a made-up table; nothing is written in the tree.
The accelerator arguments default to `-accel kvm -cpu host` where /dev/kvm is writable, QEMU's TCG otherwise; a
debug kernel runs no test (`lpl.test=none`). BOOT_SECONDS (120) bounds the boot; on a mismatch, the boot's
`acpi.cst`, `acpi` and `machine` records are printed after the differences.

Exit status: 0 when the records are the expected ones, 1 when one differs or is missing, 2 on a usage error, a
missing kernel or iasl, or a table the sandbox cannot compile.
USAGE
}

readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly IASL="$ROOT/build/acpi-corpus/acpica/generate/unix/bin/iasl"
readonly SANDBOX="$ROOT/tools/acpi/sandbox.sh"
readonly SOURCE="$ROOT/tools/acpi/boot-cst.asl"
work=""
readonly EXPECTED='[LPLTLM] cstate hint=0 latency_us=1 residency_us=1 residency_source=rule
[LPLTLM] cstate hint=1 latency_us=33 residency_us=99 residency_source=rule
[LPLTLM] cstate hint=2 latency_us=150 residency_us=450 residency_source=rule
[LPLTLM] cstate hint=3 latency_us=10 residency_us=10 residency_source=rule'

# Boots the kernel $1 with the table $2, its serial log in $3, the accelerator arguments after them, until the boot has
# printed its C-states and the record after them, or BOOT_SECONDS pass.
boot() {
    local kernel="$1" table="$2" log="$3"
    shift 3
    timeout "${BOOT_SECONDS:-120}" qemu-system-i386 -machine q35 -m 256M -smp 2 -display none -no-reboot \
        -serial file:"$log" "$@" -acpitable file="$table" -kernel "$kernel" -append "lpl.test=none" \
        >/dev/null 2>&1 &
    local qemu=$!
    while kill -0 "$qemu" 2>/dev/null; do
        grep -aq '^\[LPLTLM\] cstates ' "$log" 2>/dev/null &&
            grep -aA8 '^\[LPLTLM\] cstates ' "$log" | grep -aqv '^\[LPLTLM\] cstate' && break
        sleep 0.5
    done
    kill "$qemu" 2>/dev/null
    wait "$qemu" 2>/dev/null
}

main() {
    if [[ $# -lt 1 || "$1" == "--help" || "$1" == "-h" ]]; then
        usage
        [[ $# -ge 1 ]] && exit 0
        exit 2
    fi
    local kernel="$1"
    shift
    [[ -f "$kernel" ]] || { echo "boot-cst: no kernel at $kernel" >&2; exit 2; }
    [[ -x "$IASL" ]] || { echo "boot-cst: no iasl at ${IASL#"$ROOT"/}: run tools/acpi/corpus.sh fetch acpica" >&2; exit 2; }
    work="$(mktemp -d)"
    trap 'rm -rf -- "$work"' EXIT
    if ! "$SANDBOX" "$work" "$IASL" -oa -r 1 -we -vr -vs -p "$work/boot-cst" "$SOURCE" >"$work/iasl.log" 2>&1 ||
        [[ -L "$work/boot-cst.aml" || ! -f "$work/boot-cst.aml" ]]; then
        echo "boot-cst: iasl, in its sandbox, does not compile ${SOURCE#"$ROOT"/}:" >&2
        tr -d '\000-\010\013-\037\177' <"$work/iasl.log" | sed 's/^/    # /' >&2
        exit 2
    fi
    if [[ $# -eq 0 ]]; then
        if [[ -w /dev/kvm ]]; then set -- -accel kvm -cpu host; else set -- -accel tcg; fi
    fi
    boot "$kernel" "$work/boot-cst.aml" "$work/serial.log" "$@"
    local summary hints failed=0
    summary="$(grep -a '^\[LPLTLM\] cstates ' "$work/serial.log" || true)"
    hints="$(grep -a '^\[LPLTLM\] cstate ' "$work/serial.log" || true)"
    echo "${summary:-no cstates record}"
    if [[ " $summary " != *" slot=0 "* || " $summary " != *" published=0x0000000F "* ]]; then
        echo "not ok: the cstates record does not name slot 0 and hints 0 to 3"
        failed=1
    fi
    if [[ "$hints" != "$EXPECTED" ]]; then
        echo "not ok: the cstate records differ from the expected ones"
        diff <(echo "$EXPECTED") <(echo "$hints") | sed 's/^/    # /'
        failed=1
    fi
    if [[ $failed -ne 0 ]]; then
        grep -aE '^\[LPLTLM\] (acpi\.cst|acpi |machine )' "$work/serial.log" | sed 's/^/    # /'
        exit 1
    fi
    echo "ok: the boot printed slot 0, hints 0 to 3 and their latencies and residencies as expected"
}

main "$@"
