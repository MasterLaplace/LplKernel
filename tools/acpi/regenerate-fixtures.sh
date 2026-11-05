#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/regenerate-fixtures.sh [--write]

The made-up tables of the ACPI tests, written in ASL (specification §8.2). Each source
kernel/tests/acpi/fixtures/<suite>/<name>.asl is compiled with the pinned iasl, as `iasl -oa -r 1`
(§8.6), in the sandbox of tools/acpi/sandbox.sh, and its AML, the 36 bytes of the table header left
out, becomes the array <name>_aml of the header kernel/tests/acpi/fixtures/<suite>.h, which <suite>.c
includes. The first /** */ block of a source documents its array there.

A source compiles without a warning and without an error, but for those it names on their own line with
iasl's __EXPECT__(<id>): the ones its table makes on purpose. Remarks are not reported.

  (no option)  compiles every source and compares the header it would write with the one committed,
               byte for byte; a header that differs is printed as a diff
  --write      writes the headers, once a source is added or changed

iasl is the one `tools/acpi-corpus.sh fetch acpica` builds in build/acpi-corpus/acpica/, at the commit
that script pins: another iasl may emit other bytes. A header keeps the @date it was created on.

Exit status: 0 when every header holds what its sources compile to, 1 when one differs, is missing or
has no source, when a source does not compile, when iasl is missing or built at another commit, or when
the sandbox cannot run; 2 on a usage error.
USAGE
}

export LC_ALL=C
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly FIXTURES="$ROOT/kernel/tests/acpi/fixtures"
readonly ACPICA="$ROOT/build/acpi-corpus/acpica"
readonly IASL="$ACPICA/generate/unix/bin/iasl"
readonly SANDBOX="$ROOT/tools/acpi/sandbox.sh"
readonly TABLE_HEADER_BYTES=36
readonly BYTES_PER_LINE=16
readonly IDENTIFIER='^[a-z][a-z0-9_]*$'
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "$WORK"' EXIT

# The ACPICA commit tools/acpi-corpus.sh pins.
pinned_acpica_commit() {
    sed -n 's/^readonly ACPICA_COMMIT="\([0-9a-f]\{40\}\)"$/\1/p' "$ROOT/tools/acpi-corpus.sh"
}

# Exits when iasl is missing or built at another commit than the pinned one.
require_pinned_iasl() {
    local pinned built
    pinned="$(pinned_acpica_commit)"
    if [[ -z "$pinned" ]]; then
        echo "not ok: tools/acpi-corpus.sh pins no ACPICA commit" >&2
        exit 1
    fi
    if [[ ! -x "$IASL" ]]; then
        echo "not ok: no iasl at ${IASL#"$ROOT"/}: run tools/acpi-corpus.sh fetch acpica first" >&2
        exit 1
    fi
    built="$(git -C "$ACPICA" rev-parse HEAD)"
    if [[ "$built" != "$pinned" ]]; then
        echo "not ok: iasl is built at ACPICA $built, not at the pinned $pinned: run tools/acpi-corpus.sh" \
            "fetch acpica" >&2
        exit 1
    fi
}

# Exits, with what the sandbox printed, when it cannot run a command: iasl never runs outside it.
require_sandbox() {
    if ! "$SANDBOX" "$WORK" true; then
        echo "not ok: the sandbox of tools/acpi/sandbox.sh cannot run, and iasl runs in no other place" >&2
        exit 1
    fi
}

# Compiles the source $1 into $2.aml, in the sandbox, or prints iasl's report and fails.
compile() {
    local source="$1" prefix="$2"
    if ! "$SANDBOX" "${prefix%/*}" "$IASL" -oa -r 1 -we -vr -vs -p "$prefix" "$source" >"$prefix.log" 2>&1 ||
        [[ ! -f "$prefix.aml" ]]; then
        echo "not ok ${source#"$ROOT"/}: iasl refuses it"
        sed 's/^/    # /' "$prefix.log"
        return 1
    fi
    if [[ "$(stat -c %s "$prefix.aml")" -le "$TABLE_HEADER_BYTES" ]]; then
        echo "not ok ${source#"$ROOT"/}: compiles to a table header and no AML"
        return 1
    fi
}

# Prints the first /** */ block of the source $1, as it stands.
documentation() {
    awk '/^\/\*\*$/ { inside = 1 } inside { print } inside && /^ \*\/$/ { exit }' "$1"
}

# Prints the AML of the table $1, its header left out, as the lines of a C initialiser laid out as clang-format lays
# out a list with a trailing comma, so that the formatter leaves the header as written: one byte per line when there
# are 2 to 4, else as few columns as fill the fewest lines of at most BYTES_PER_LINE.
initialiser() {
    tail -c +"$((TABLE_HEADER_BYTES + 1))" "$1" | od -An -v -tx1 | tr -s ' ' '\n' |
        awk -v most="$BYTES_PER_LINE" 'NF { bytes[count++] = toupper($1) } END {
            lines = int((count + most - 1) / most)
            per_line = (count >= 2 && count <= 4) ? 1 : int((count + lines - 1) / lines)
            for (byte = 0; byte < count; ++byte)
                printf "%s0x%su,%s", (byte % per_line == 0 ? "    " : " "), bytes[byte],
                    (byte % per_line == per_line - 1 || byte == count - 1 ? "\n" : "")
        }'
}

# Prints the @date of the header $1 when it exists, else today's.
creation_date() {
    local date=""
    [[ -f "$1" ]] && date="$(sed -n 's/^ \* @date \([0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\)$/\1/p' "$1")"
    [[ -n "$date" ]] && echo "$date" || date +%F
}

# Prints the licence block and the guard's opening of the header of the suite $1, created on $2.
header_opening() {
    local suite="$1" created="$2" guard="KERNEL_TESTS_ACPI_FIXTURES_${1^^}_H"
    cat <<HEADER
/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © ${created:0:4} by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file ${suite}.h
 * @brief The made-up tables of kernel/tests/acpi/${suite}.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/${suite}/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date ${created}
 **************************************************************************/

#ifndef ${guard}
#define ${guard}

#include <stdint.h>
HEADER
}

# Writes into $WORK the header of the suite directory $1, or prints why it cannot and fails.
generate_suite() {
    local directory="$1" suite="${1##*/}" source name documented failures=0
    local output="$WORK/$suite.h"
    local sources=("$directory"/*.asl)
    if [[ ! "$suite" =~ $IDENTIFIER ]]; then
        echo "not ok ${directory#"$ROOT"/}: a suite is named as its .c file, in lower case"
        return 1
    fi
    if [[ ! -f "${sources[0]}" ]]; then
        echo "not ok ${directory#"$ROOT"/}: holds no .asl source"
        return 1
    fi
    mkdir -p "$WORK/$suite"
    header_opening "$suite" "$(creation_date "$FIXTURES/$suite.h")" >"$output"
    for source in "${sources[@]}"; do
        name="$(basename "$source" .asl)"
        documented="$(documentation "$source")"
        if [[ ! "$name" =~ $IDENTIFIER ]]; then
            echo "not ok ${source#"$ROOT"/}: a source is named as its array, in lower case, without _aml"
            failures=$((failures + 1))
            continue
        fi
        if [[ -z "$documented" ]]; then
            echo "not ok ${source#"$ROOT"/}: has no /** */ block to document its array"
            failures=$((failures + 1))
            continue
        fi
        compile "$source" "$WORK/$suite/$name" || { failures=$((failures + 1)); continue; }
        printf '\n%s\nstatic const uint8_t %s_aml[] = {\n%s\n};\n' "$documented" "$name" \
            "$(initialiser "$WORK/$suite/$name.aml")" >>"$output"
    done
    printf '\n#endif /* KERNEL_TESTS_ACPI_FIXTURES_%s_H */\n' "${suite^^}" >>"$output"
    [[ "$failures" -eq 0 ]]
}

# Compares each header written into $WORK with the one committed; prints a line per header.
compare_headers() {
    local failures=0 generated committed
    for generated in "$WORK"/*.h; do
        committed="$FIXTURES/${generated##*/}"
        if [[ ! -f "$committed" ]]; then
            echo "not ok ${committed#"$ROOT"/}: missing, run tools/acpi/regenerate-fixtures.sh --write"
            failures=$((failures + 1))
        elif ! cmp -s "$generated" "$committed"; then
            echo "not ok ${committed#"$ROOT"/}: differs from what its sources compile to"
            diff -u "$committed" "$generated" | sed 's/^/    # /' || true
            failures=$((failures + 1))
        else
            echo "ok ${committed#"$ROOT"/}"
        fi
    done
    [[ "$failures" -eq 0 ]]
}

# Fails on a committed header whose suite has no source directory.
refuse_orphan_headers() {
    local failures=0 committed
    for committed in "$FIXTURES"/*.h; do
        [[ -f "$committed" ]] || continue
        if [[ ! -d "${committed%.h}" ]]; then
            echo "not ok ${committed#"$ROOT"/}: has no source directory, $(basename "$committed" .h)/"
            failures=$((failures + 1))
        fi
    done
    [[ "$failures" -eq 0 ]]
}

main() {
    local write=false failures=0 directory
    case "${1:-}" in
        "") ;;
        --write) write=true ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    [[ $# -le 1 ]] || { usage >&2; exit 2; }
    require_pinned_iasl
    require_sandbox
    local directories=("$FIXTURES"/*/)
    if [[ ! -d "${directories[0]}" ]]; then
        echo "not ok: no fixture directory in ${FIXTURES#"$ROOT"/}"
        exit 1
    fi
    for directory in "${directories[@]}"; do
        generate_suite "${directory%/}" || failures=$((failures + 1))
    done
    refuse_orphan_headers || failures=$((failures + 1))
    [[ "$failures" -eq 0 ]] || exit 1
    if [[ "$write" == true ]]; then
        cp "$WORK"/*.h "$FIXTURES"/
        echo "wrote $(find "$WORK" -maxdepth 1 -name '*.h' | wc -l) header(s) in ${FIXTURES#"$ROOT"/}"
        exit 0
    fi
    compare_headers || exit 1
}

main "$@"
