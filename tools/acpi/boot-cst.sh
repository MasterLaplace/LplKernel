#!/usr/bin/env bash
set -u

usage() {
    cat <<'USAGE'
Usage: tools/acpi/boot-cst.sh [--ports] <kernel> [<qemu accelerator argument>...]

Boots <kernel>, a debug or release image, on QEMU q35 (256 MiB, two processors) with tools/acpi/boot-cst.asl loaded
by -acpitable: a _CST for QEMU's processors, which QEMU gives none. Then checks the C-states the boot prints: the
`[LPLTLM] cstates` record names CPU slot 0 and publishes hints 0 to 3, and the `[LPLTLM] cstate` records are those
the source's documentation works out by hand, from _CST's evaluation to each hint's latency and residency. The
machine-dependent keys, `monitor`, `enumerated` and `usable`, are printed and not compared.

With --ports, the table is tools/acpi/boot-cst-ports.asl, whose processors list AMD's C1, C2 and C3, the last two as
port reads. Then `cstates` publishes hint 0, `cstate` gives it 1 us, and the `[LPLTLM] sleep.depths` and
`[LPLTLM] sleep.depth` records give the ladder the `[LPLTLM] machine` record calls for: where the processor offers no
invariant TSC, C1 alone and `ports=counter_may_stop`; where it does, C1 then port 0x414 of 18 us, then, where
`sleep.depths` says the processor enters a type 3 behind the bus-master check (`type_3=1`), port 0x415 of 350 us with
the check, and the C3 refused otherwise. Depth 0's entry, HLT or MWAIT 0, and `type_3`, which the vendor rule decides,
are printed and not compared. The accelerator arguments then default to
`-accel kvm -cpu host,+invtsc` where /dev/kvm is writable, `-accel tcg -cpu max` otherwise, whose TCG offers no
invariant TSC.

With either table, the `[LPLTLM] park` record of the second processor parks it at depth 0, nothing inherited or
refused and no port read, after one turn at least: no probe has validated a port when the boot prints it. Its entry
and target, HLT or MWAIT 0, and how many turns it took are printed and not compared.

BOOT_TESTS names the debug kernel's tests the boot runs (`lpl.test=<BOOT_TESTS>`, `none` by default); with it, the
boot waits for their `# Totals:` line, which must count no failure.

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
work=""
readonly EXPECTED='[LPLTLM] cstate hint=0 latency_us=1 residency_us=1 residency_source=rule
[LPLTLM] cstate hint=1 latency_us=33 residency_us=99 residency_source=rule
[LPLTLM] cstate hint=2 latency_us=150 residency_us=450 residency_source=rule
[LPLTLM] cstate hint=3 latency_us=10 residency_us=10 residency_source=rule'
readonly EXPECTED_PORTS='[LPLTLM] cstate hint=0 latency_us=1 residency_us=1 residency_source=rule'
readonly LADDER_PORTS='[LPLTLM] sleep.depths available=0x00000003 ports=taken ports_refused=1
[LPLTLM] sleep.depth depth=0 latency_us=1 bus_master_check=0
[LPLTLM] sleep.depth depth=1 entry=port target=0x00000414 latency_us=18 bus_master_check=0'
readonly LADDER_PORTS_TYPE_3='[LPLTLM] sleep.depths available=0x00000007 ports=taken ports_refused=0
[LPLTLM] sleep.depth depth=0 latency_us=1 bus_master_check=0
[LPLTLM] sleep.depth depth=1 entry=port target=0x00000414 latency_us=18 bus_master_check=0
[LPLTLM] sleep.depth depth=2 entry=port target=0x00000415 latency_us=350 bus_master_check=1'
readonly LADDER_NO_COUNTER='[LPLTLM] sleep.depths available=0x00000001 ports=counter_may_stop ports_refused=0
[LPLTLM] sleep.depth depth=0 latency_us=1 bus_master_check=0'
readonly PARKED='[LPLTLM] park slot=1 depth=0 inherited=0x00000000 refused=0x00000000 turns=some port_reads=0 quick=0 stuck=0'

# Whether the serial log $1 holds all the boot is waited for: the C-states and the record after the ladder, then, when
# BOOT_TESTS is set, the tests' totals.
booted() {
    grep -aq '^\[LPLTLM\] sleep\.depths ' "$1" 2>/dev/null &&
        grep -aA9 '^\[LPLTLM\] sleep\.depths ' "$1" | grep -aqv '^\[LPLTLM\] sleep\.depth' &&
        { [[ -z "${BOOT_TESTS:-}" ]] || grep -aq '^# Totals:' "$1"; }
}

# Boots the kernel $1 with the table $2, its serial log in $3, the accelerator arguments after them, until booted holds
# or BOOT_SECONDS pass.
boot() {
    local kernel="$1" table="$2" log="$3"
    shift 3
    timeout "${BOOT_SECONDS:-120}" qemu-system-i386 -machine q35 -m 256M -smp 2 -display none -no-reboot \
        -serial file:"$log" "$@" -acpitable file="$table" -kernel "$kernel" -append "lpl.test=${BOOT_TESTS:-none}" \
        >/dev/null 2>&1 &
    local qemu=$!
    while kill -0 "$qemu" 2>/dev/null; do
        booted "$log" && break
        sleep 0.5
    done
    kill "$qemu" 2>/dev/null
    wait "$qemu" 2>/dev/null
}

# The ladder records of the serial log $1, the entry and the target of depth 0 and the processor's type-3 rule left out.
ladder() {
    grep -a '^\[LPLTLM\] sleep\.depth' "$1" |
        sed -E 's/^(\[LPLTLM\] sleep\.depth depth=0) entry=[a-z]+ target=0x[0-9A-F]+/\1/; s/ type_3=[01]$//'
}

# The park records of the serial log $1, their entry and target, which depend on the machine, left out, and their turns
# as `some` when the processor took one at least: how many depends on the boot.
parks() {
    grep -a '^\[LPLTLM\] park ' "$1" | sed -E 's/ entry=[a-z]+ target=0x[0-9A-F]+//; s/ turns=[1-9][0-9]*/ turns=some/'
}

main() {
    if [[ $# -lt 1 || "$1" == "--help" || "$1" == "-h" ]]; then
        usage
        [[ $# -ge 1 ]] && exit 0
        exit 2
    fi
    local ports=0 source="$ROOT/tools/acpi/boot-cst.asl" expected="$EXPECTED" published=0x0000000F
    if [[ "$1" == "--ports" ]]; then
        ports=1 source="$ROOT/tools/acpi/boot-cst-ports.asl" expected="$EXPECTED_PORTS" published=0x00000001
        shift
        [[ $# -ge 1 ]] || { usage; exit 2; }
    fi
    local kernel="$1"
    shift
    [[ -f "$kernel" ]] || { echo "boot-cst: no kernel at $kernel" >&2; exit 2; }
    [[ -x "$IASL" ]] || { echo "boot-cst: no iasl at ${IASL#"$ROOT"/}: run tools/acpi/corpus.sh fetch acpica" >&2; exit 2; }
    work="$(mktemp -d)"
    trap 'rm -rf -- "$work"' EXIT
    if ! "$SANDBOX" "$work" "$IASL" -oa -r 1 -we -vr -vs -p "$work/boot-cst" "$source" >"$work/iasl.log" 2>&1 ||
        [[ -L "$work/boot-cst.aml" || ! -f "$work/boot-cst.aml" ]]; then
        echo "boot-cst: iasl, in its sandbox, does not compile ${source#"$ROOT"/}:" >&2
        tr -d '\000-\010\013-\037\177' <"$work/iasl.log" | sed 's/^/    # /' >&2
        exit 2
    fi
    if [[ $# -eq 0 && $ports -eq 1 ]]; then
        if [[ -w /dev/kvm ]]; then set -- -accel kvm -cpu host,+invtsc; else set -- -accel tcg -cpu max; fi
    elif [[ $# -eq 0 ]]; then
        if [[ -w /dev/kvm ]]; then set -- -accel kvm -cpu host; else set -- -accel tcg; fi
    fi
    boot "$kernel" "$work/boot-cst.aml" "$work/serial.log" "$@"
    local summary hints machine wanted totals failed=0
    summary="$(grep -a '^\[LPLTLM\] cstates ' "$work/serial.log" || true)"
    hints="$(grep -a '^\[LPLTLM\] cstate ' "$work/serial.log" || true)"
    machine="$(grep -a '^\[LPLTLM\] machine ' "$work/serial.log" || true)"
    echo "${summary:-no cstates record}"
    if [[ " $summary " != *" slot=0 "* || " $summary " != *" published=$published "* ]]; then
        echo "not ok: the cstates record does not name slot 0 and publish $published"
        failed=1
    fi
    if [[ "$hints" != "$expected" ]]; then
        echo "not ok: the cstate records differ from the expected ones"
        diff <(echo "$expected") <(echo "$hints") | sed 's/^/    # /'
        failed=1
    fi
    if [[ $ports -eq 1 ]]; then
        wanted="$LADDER_PORTS"
        grep -aq '^\[LPLTLM\] sleep\.depths .* type_3=1$' "$work/serial.log" && wanted="$LADDER_PORTS_TYPE_3"
        [[ " $machine " == *" tsc_invariant=0 "* ]] && wanted="$LADDER_NO_COUNTER"
        ladder "$work/serial.log"
        if [[ "$(ladder "$work/serial.log")" != "$wanted" ]]; then
            echo "not ok: the ladder differs from the one the machine record calls for"
            diff <(echo "$wanted") <(ladder "$work/serial.log") | sed 's/^/    # /'
            failed=1
        fi
    fi
    parks "$work/serial.log"
    if [[ "$(parks "$work/serial.log")" != "$PARKED" ]]; then
        echo "not ok: the second processor's park differs from the expected one"
        diff <(echo "$PARKED") <(parks "$work/serial.log") | sed 's/^/    # /'
        failed=1
    fi
    if [[ -n "${BOOT_TESTS:-}" ]]; then
        totals="$(grep -a '^# Totals:' "$work/serial.log" || true)"
        echo "${totals:-no # Totals: line}"
        grep -aE '^ *not ok ' "$work/serial.log" | sed 's/^/    # /'
        [[ "$totals" == *" fail:0 "* ]] || { echo "not ok: the tests BOOT_TESTS names fail or did not finish"; failed=1; }
    fi
    if [[ $failed -ne 0 ]]; then
        grep -aE '^\[LPLTLM\] (acpi\.cst|acpi |machine )' "$work/serial.log" | sed 's/^/    # /'
        exit 1
    fi
    echo "ok: the boot printed the slot, the hints, their latencies and residencies$([[ $ports -eq 1 ]] && echo ", the ladder") and the park as expected"
}

main "$@"
