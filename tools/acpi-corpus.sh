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
         build/acpi-corpus/full.list); each report's decoded tables come too, kept in the clone's
         objects and never checked out
  check  walks each machine of a list with `test-acpi --machine`, built here, and compares its
         directory with what tools/acpi/expected_directory.py reads from the same dump, its MADT
         with what tools/acpi/expected_madt.py reads from the report's decoded tables, and the
         opcodes its definition blocks decode with what tools/acpi/aml_census.py counts; then
         tools/acpi/check_against_iasl.py compares the census's declarations, table by table, with
         the report's iASL disassembly, and lists the tables where they differ

Exit status: 0 when every machine agrees, 1 when one does not or its dump or decoded tables are
missing, 2 on a usage error.
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

# Fetches, in one request, the decoded reports of the list $1 the clone lacks: the check reads them from the
# object store, where they stay compressed and out of the work tree. A fetch given no object would fetch the
# remote's branches instead, so none is made when nothing is missing; a report the pinned commit lacks stops it.
fetch_decoded() {
    local wanted present missing
    wanted="$(reports "$1" | while IFS= read -r report; do
        git -C "$LINUXHW" rev-parse --verify --quiet "$LINUXHW_COMMIT:$report" ||
            { echo "fetch: $report: no decoded report at $LINUXHW_COMMIT" >&2; exit 1; }
    done)" || exit 1
    present="$(git -C "$LINUXHW" cat-file --batch-all-objects --batch-check='%(objectname)' 2>/dev/null)"
    missing="$(comm -23 <(sort -u <<<"$wanted") <(sort -u <<<"$present"))"
    [[ -n "$missing" ]] || return 0
    git -C "$LINUXHW" -c fetch.negotiationAlgorithm=noop fetch -q --no-tags --no-write-fetch-head \
        --recurse-submodules=no --filter=blob:none --stdin origin <<<"$missing"
}

# Fetches the pinned commit's trees alone, then checks out the dumps of the list $1 and fetches their decoded
# reports.
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
    fetch_decoded "$1"
    echo "$(reports "$1" | wc -l) dumps in the $1 list, $(find "$LINUXHW" -name '*.bin' -not -path '*/.git/*' | wc -l) in $LINUXHW"
    echo "ACPI dumps collected by hw-probe users at linux-hardware.org, published by linuxhw under CC-BY-4.0;"
    echo "changed: converted from acpidump text to binary tables."
}

# Tells whether the clone holds the decoded report of the machine $1, never fetching it from git 2.45 on;
# an older git fetches it instead.
has_decoded() {
    GIT_NO_LAZY_FETCH=1 git -C "$LINUXHW" cat-file -e "$LINUXHW_COMMIT:$1" 2>/dev/null
}

# Prints the decoded report of the machine $1 from the clone's objects.
decoded() {
    GIT_NO_LAZY_FETCH=1 git -C "$LINUXHW" cat-file blob "$LINUXHW_COMMIT:$1"
}

# Compares the directory and the MADT the kernel's code reads from each dump of the list $1 with the expected
# ones, then says how many CPU slots, I/O APICs, overrides and NMI connections the comparison held.
check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 compared="" disassembly="" machine dump actual expected
    while IFS= read -r machine; do
        number=$((number + 1))
        dump="$LINUXHW/$machine.bin"
        if [[ ! -f "$dump" ]]; then
            echo "not ok $number $machine: no dump, run fetch first"
            continue
        fi
        if ! has_decoded "$machine"; then
            echo "not ok $number $machine: no decoded report, run fetch first"
            continue
        fi
        actual="$("$HARNESS" --machine "$dump" 2>&1)" || true
        expected="$(python3 -I "$ROOT/tools/acpi/expected_directory.py" "$dump")
$(decoded "$machine" | python3 -I "$ROOT/tools/acpi/expected_madt.py" "$dump" -)
$(python3 -I "$ROOT/tools/acpi/expected_machine_language.py" "$dump")" || true
        disassembly+="$(decoded "$machine" | python3 -I "$ROOT/tools/acpi/check_against_iasl.py" "$dump" - |
            sed "s|^|$machine |")"$'\n' || true
        compared+="$expected"$'\n'
        if [[ "$actual" == "$expected" ]]; then
            echo "ok $number $machine"
            agree=$((agree + 1))
        else
            echo "not ok $number $machine"
            diff <(echo "$expected") <(echo "$actual") | sed 's/^/    # /' || true
        fi
    done < <(reports "$1")
    echo "# directory and MADT: $agree of $number machines agree"
    echo "# compared: $(grep -c '^processor ' <<<"$compared") CPU slots," \
        "$(grep -c '^io_apic ' <<<"$compared") I/O APICs, $(grep -c '^override ' <<<"$compared") overrides," \
        "$(grep -c '^nmi ' <<<"$compared") NMI connections, $(grep -c '^aml opcode ' <<<"$compared") opcode counts"
    echo "# iASL disassembly: $(grep -c ' iasl agrees ' <<<"$disassembly") tables declare as many objects," \
        "$(grep -c ' iasl differs ' <<<"$disassembly") differ, $(grep -c ' iasl missing ' <<<"$disassembly") have none"
    grep ' iasl differs ' <<<"$disassembly" | sed 's/^/#   /' || true
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]]
}

case "${1:-}" in
    fetch) known_list "${2:-corpus}"; mkdir -p "$CORPUS"; fetch "${2:-corpus}" ;;
    check) known_list "${2:-corpus}"; check "${2:-corpus}" ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
