#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi-corpus.sh fetch [quick|corpus|full]
       tools/acpi-corpus.sh check [quick|corpus|full]

The firmware of recorded machines, which the ACPI layer is checked on. linuxhw/ACPI is fetched at a
pinned commit into build/acpi-corpus/, never committed and never put in an image.

  fetch  adds the dumps of a list to those already fetched: quick (tools/acpi/quick.list), corpus
         (tools/acpi/corpus.list, the default) or full (every report with a dump, written to
         build/acpi-corpus/full.list)
  check  walks each machine of a list with `test-acpi --machine`, built here, and compares its
         directory with what tools/acpi/expected_directory.py reads from the same dump

Exit status: 0 when every machine agrees, 1 when one does not or its dump is missing, 2 on a usage
error.
USAGE
}

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly CORPUS="$ROOT/build/acpi-corpus"
readonly LINUXHW="$CORPUS/linuxhw"
readonly LINUXHW_URL="https://github.com/linuxhw/ACPI.git"
readonly LINUXHW_COMMIT="d6d30c6ef6f45506567f25a302c64f009d8f5f4e"
readonly HARNESS="$ROOT/build/acpi-host/test-acpi"

# Refuses, with the usage, a list that is not quick, corpus or full.
known_list() {
    case "$1" in
        quick | corpus | full) ;;
        *) usage >&2; exit 2 ;;
    esac
}

# Prints the reports of a list, one per line, without its comments.
reports() {
    case "$1" in
        quick | corpus) grep -v '^#' "$ROOT/tools/acpi/$1.list" ;;
        full) cat "$CORPUS/full.list" ;;
    esac
}

# Prints the sparse-checkout pattern that names exactly the dump of each report read on stdin.
dump_patterns() {
    sed -e 's|[][\*?!#]|\\&|g' -e 's|^\(.*\)$|/\1.bin|'
}

# Fetches the pinned commit's trees alone, then checks out the dumps of the list $1.
fetch() {
    if [[ ! -d "$LINUXHW/.git" ]]; then
        git init -q "$LINUXHW"
        git -C "$LINUXHW" remote add origin "$LINUXHW_URL"
    fi
    git -C "$LINUXHW" fetch -q --depth 1 --filter=blob:none origin "$LINUXHW_COMMIT"
    git -C "$LINUXHW" ls-tree -r -z --name-only "$LINUXHW_COMMIT" | tr '\0' '\n' | sed -n 's/\.bin$//p' \
        >"$CORPUS/full.list"
    { git -C "$LINUXHW" sparse-checkout list 2>/dev/null || true; reports "$1" | dump_patterns; } | sort -u |
        git -C "$LINUXHW" sparse-checkout set --no-cone --stdin
    git -C "$LINUXHW" checkout -q --detach "$LINUXHW_COMMIT"
    echo "$(reports "$1" | wc -l) dumps in the $1 list, $(find "$LINUXHW" -name '*.bin' -not -path '*/.git/*' | wc -l) in $LINUXHW"
    echo "ACPI dumps collected by hw-probe users at linux-hardware.org, published by linuxhw under CC-BY-4.0;"
    echo "changed: converted from acpidump text to binary tables."
}

# Compares the directory the kernel's code reads from each dump of the list $1 with the expected one.
check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 machine dump actual expected
    while IFS= read -r machine; do
        number=$((number + 1))
        dump="$LINUXHW/$machine.bin"
        if [[ ! -f "$dump" ]]; then
            echo "not ok $number $machine: no dump, run fetch first"
            continue
        fi
        actual="$("$HARNESS" --machine "$dump" 2>&1)" || true
        expected="$(python3 -I "$ROOT/tools/acpi/expected_directory.py" "$dump")"
        if [[ "$actual" == "$expected" ]]; then
            echo "ok $number $machine"
            agree=$((agree + 1))
        else
            echo "not ok $number $machine"
            diff <(echo "$expected") <(echo "$actual") | sed 's/^/    # /' || true
        fi
    done < <(reports "$1")
    echo "# directory: $agree of $number machines agree"
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]]
}

case "${1:-}" in
    fetch) known_list "${2:-corpus}"; mkdir -p "$CORPUS"; fetch "${2:-corpus}" ;;
    check) known_list "${2:-corpus}"; check "${2:-corpus}" ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
