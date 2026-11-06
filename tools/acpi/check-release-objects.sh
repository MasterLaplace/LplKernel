#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/check-release-objects.sh [<object>...]
       tools/acpi/check-release-objects.sh --self-test

Checks the release objects of the ACPI layer, by default kernel/kernel/acpi/*.o as `./clean.sh` then
`SMOKE_TESTS=0 ./build.sh` leave them (specification §7.2):
  O15  no instruction carries a lock prefix: one owner runs AML (§3.7), so the interpreter needs no atomic;
  O16  no object defines or calls a log function below the release level,
       advanced_configuration_and_power_interface_log_debug (kernel/include/kernel/acpi/log.h).
OBJDUMP and NM name the tools (default i686-elf-objdump and i686-elf-nm). --self-test assembles, with AS
(default i686-elf-as), an object with a lock prefix, one that calls the debug log function and a clean
one, and checks that the first two are refused and the third passes.

Exit status: 0 when every object passes, 1 when one does not or when there is none, 2 on a usage error.
USAGE
}

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly OBJDUMP="${OBJDUMP:-i686-elf-objdump}" NM="${NM:-i686-elf-nm}" AS="${AS:-i686-elf-as}"
readonly BELOW_RELEASE=(advanced_configuration_and_power_interface_log_debug)

# Prints the functions of the object $1 that hold an instruction with a lock prefix, one per line.
locked_functions() {
    "$OBJDUMP" -d --no-show-raw-insn "$1" |
        awk '/^[0-9a-f]+ <.*>:$/ { name = $2; gsub(/[<>:]/, "", name) } /^ *[0-9a-f]+:\t *lock / { print name }' |
        sort -u
}

# Prints the log functions below the release level that the object $1 defines or calls, one per line.
logs_below_release() {
    "$NM" "$1" | awk '{ print $NF }' | grep -Fx -f <(printf '%s\n' "${BELOW_RELEASE[@]}") || true
}

# Checks each object $@ for O15 and O16, prints a line per object and a total, and fails when one fails
# or when there is none.
check() {
    local failures=0 object locked logged
    [[ "$#" -gt 0 ]] || { echo "not ok: no release object of the ACPI layer; build it with ./clean.sh then" \
        "SMOKE_TESTS=0 ./build.sh"; return 1; }
    for object in "$@"; do
        if [[ ! -f "$object" ]]; then
            echo "not ok $object: no such object"
            failures=$((failures + 1))
            continue
        fi
        locked="$(locked_functions "$object")"
        logged="$(logs_below_release "$object")"
        if [[ -n "$locked" ]]; then
            echo "not ok $object: a lock prefix in $(paste -sd ' ' <<<"$locked") (O15)"
            failures=$((failures + 1))
        fi
        if [[ -n "$logged" ]]; then
            echo "not ok $object: $(paste -sd ' ' <<<"$logged"), below the release level (O16)"
            failures=$((failures + 1))
        fi
        [[ -n "$locked" || -n "$logged" ]] || echo "ok $object"
    done
    echo "# release objects: $# checked, $failures failures"
    [[ "$failures" -eq 0 ]]
}

# Assembles the source $2 into the object $1.
assemble() {
    "$AS" --32 -o "$1" <<<"$2"
}

# Checks that check refuses an object with a lock prefix, one that calls the debug log function and an
# empty list, each for its own reason, and passes a clean one.
self_test() {
    local work failures=0 output
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' RETURN
    assemble "$work/clean.o" $'.text\n.globl clean\nclean:\n\tincl (%eax)\n\tret'
    assemble "$work/locked.o" $'.text\n.globl locked\nlocked:\n\tlock incl (%eax)\n\tret'
    assemble "$work/logged.o" $'.text\n.globl logged\nlogged:\n\tcall advanced_configuration_and_power_interface_log_debug\n\tret'
    if ! output="$(check "$work/clean.o")"; then
        echo "not ok self-test: a clean object is refused: $output"
        failures=$((failures + 1))
    fi
    if output="$(check "$work/locked.o")" || ! grep -q 'a lock prefix in locked (O15)' <<<"$output"; then
        echo "not ok self-test: an object with a lock prefix is not refused for O15: $output"
        failures=$((failures + 1))
    fi
    if output="$(check "$work/logged.o")" || ! grep -q 'below the release level (O16)' <<<"$output"; then
        echo "not ok self-test: an object that calls the debug log function is not refused for O16: $output"
        failures=$((failures + 1))
    fi
    if output="$(check)"; then
        echo "not ok self-test: an empty list passes"
        failures=$((failures + 1))
    fi
    [[ "$failures" -eq 0 ]] && echo "ok self-test: a lock prefix, a debug log call and an empty list are refused"
    [[ "$failures" -eq 0 ]]
}

case "${1:-}" in
    -h | --help) usage ;;
    --self-test) [[ "$#" -eq 1 ]] || { usage >&2; exit 2; }; self_test ;;
    -*) usage >&2; exit 2 ;;
    *)
        if [[ "$#" -eq 0 ]]; then
            shopt -s nullglob
            set -- "$ROOT"/kernel/kernel/acpi/*.o
        fi
        check "$@"
        ;;
esac
