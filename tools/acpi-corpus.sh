#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi-corpus.sh fetch [quick|corpus|full|acpica|qemu|uacpi]
       tools/acpi-corpus.sh check [quick|corpus|full]
       tools/acpi-corpus.sh namespace [quick|corpus|full|qemu]
       tools/acpi-corpus.sh closure [quick|corpus|full|qemu]
       tools/acpi-corpus.sh uacpi

The firmware of recorded machines, which the ACPI layer is checked on. linuxhw/ACPI is fetched at a
pinned commit into build/acpi-corpus/, never committed and never put in an image.

  fetch  adds the dumps of a list to those already fetched: quick (tools/acpi/quick.list), corpus
         (tools/acpi/corpus.list, the default) or full (every report with a dump, written to
         build/acpi-corpus/full.list); each report's decoded tables come too, kept in the clone's
         objects and never checked out
         acpica builds iasl, acpiexec and acpixtract from ACPICA at a pinned commit; qemu fetches the
         reference tables of QEMU at a pinned release, its 47 x86 configurations; uacpi fetches uACPI's
         test cases at a pinned commit
  check  walks each machine of a list with `test-acpi --machine`, built here, and compares its
         directory with what tools/acpi/expected_directory.py reads from the same dump, its FADT
         and its MADT with what tools/acpi/expected_fadt.py and tools/acpi/expected_madt.py read
         from the report's decoded tables, and the opcodes and method calls its definition blocks
         decode with what tools/acpi/aml_census.py counts; then
         tools/acpi/check_against_iasl.py compares the census's declarations, table by table, with
         the report's iASL disassembly, and lists the tables where they differ
  namespace  loads each machine of a list, or each QEMU configuration, with `test-acpi --namespace`
         and compares its namespace with the one acpiexec loads from the same tables; a machine whose
         table-level code fails is counted and left out, until the interpreter runs what failed
  closure  loads each machine of a list, or each QEMU configuration, with `test-acpi --closure`, and
         prints, for each opcode the needs path reaches on some machine, how many machines it reaches
         it on, and which when they are five or fewer: the opcodes the interpreter executes
         (specification §4.1); a machine not read or a decode fault fails it
  uacpi  compiles each of uACPI's test cases with the iasl acpica built and evaluates its \MAIN with
         `test-acpi --evaluate` (specification §8.5); a case passes, diverges as
         tools/acpi/uacpi-divergences.list says, is skipped, or waits for what the kernel does not run
         yet; any other result fails

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
readonly ACPICA="$CORPUS/acpica"
readonly ACPICA_URL="https://github.com/acpica/acpica.git"
readonly ACPICA_COMMIT="e85aa3bebaae81ed1e6d163511ac7e1eafe732cf"
readonly ACPICA_BIN="$ACPICA/generate/unix/bin"
readonly QEMU="$CORPUS/qemu"
readonly QEMU_URL="https://github.com/qemu/qemu.git"
readonly QEMU_COMMIT="2d3df8abca265c9bcc9e438d691d561592060998"
readonly QEMU_TABLES="$QEMU/tests/data/acpi/x86"
readonly UACPI="$CORPUS/uacpi"
readonly UACPI_URL="https://github.com/uACPI/uACPI.git"
readonly UACPI_COMMIT="c0edecef62bcac6ce3f783540099190bafadadda"

# Refuses, with the usage, a list that is not quick, corpus or full.
known_list() {
    case "$1" in
        quick | corpus | full) ;;
        *) usage >&2; exit 2 ;;
    esac
}

# Clones ACPICA at the pinned commit, its blobs fetched as the checkout needs them, and builds the three
# tools the oracles use.
fetch_acpica() {
    if [[ ! -d "$ACPICA/.git" ]]; then
        git clone -q --filter=blob:none --no-checkout "$ACPICA_URL" "$ACPICA"
    fi
    git -C "$ACPICA" fetch -q --depth 1 origin "$ACPICA_COMMIT"
    git -C "$ACPICA" checkout -q --detach "$ACPICA_COMMIT"
    make -C "$ACPICA" -s -j"$(nproc)" iasl acpiexec acpixtract >/dev/null
}

# Fetches QEMU's reference ACPI tables at the pinned release, and nothing else of QEMU.
fetch_qemu() {
    if [[ ! -d "$QEMU/.git" ]]; then
        git clone -q --filter=blob:none --no-checkout "$QEMU_URL" "$QEMU"
    fi
    git -C "$QEMU" sparse-checkout set --no-cone tests/data/acpi/x86
    git -C "$QEMU" fetch -q --depth 1 origin "$QEMU_COMMIT"
    git -C "$QEMU" checkout -q --detach "$QEMU_COMMIT"
    echo "QEMU's reference tables, GPL-2.0-or-later, used as data and never committed."
}

# Fetches uACPI's test cases at the pinned commit, and nothing else of uACPI.
fetch_uacpi() {
    if [[ ! -d "$UACPI/.git" ]]; then
        git clone -q --filter=blob:none --no-checkout "$UACPI_URL" "$UACPI"
    fi
    git -C "$UACPI" sparse-checkout set --no-cone tests/test-cases
    git -C "$UACPI" fetch -q --depth 1 origin "$UACPI_COMMIT"
    git -C "$UACPI" checkout -q --detach "$UACPI_COMMIT"
    echo "uACPI's test cases, MIT, used as data and never committed."
}

# Runs uACPI's test cases through the harness, as specification §8.5 says.
uacpi_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    [[ -x "$ACPICA_BIN/iasl" ]] || { echo "uacpi: no iasl, run fetch acpica first" >&2; exit 1; }
    [[ -d "$UACPI/tests/test-cases" ]] || { echo "uacpi: no test cases, run fetch uacpi first" >&2; exit 1; }
    python3 -I "$ROOT/tools/acpi/uacpi_cases.py" "$ACPICA_BIN/iasl" "$HARNESS" "$UACPI/tests/test-cases" \
        "$ROOT/tools/acpi/uacpi-divergences.list"
}

# Prints each QEMU configuration as its DSDT, then its SSDT when the configuration has one, one per line.
qemu_configurations() {
    local dsdt variant
    for dsdt in "$QEMU_TABLES"/{pc,q35,microvm}/DSDT*; do
        variant="${dsdt##*/DSDT}"
        if [[ -f "${dsdt%/*}/SSDT$variant" ]]; then
            printf '%s %s\n' "$dsdt" "${dsdt%/*}/SSDT$variant"
        else
            printf '%s\n' "$dsdt"
        fi
    done
}

# Compares the namespace the kernel's code loads with acpiexec's, for each machine of the list $1 or each
# QEMU configuration; a machine whose table-level code fails to run is counted apart.
namespace_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    [[ -x "$ACPICA_BIN/acpiexec" ]] || { echo "namespace: no acpiexec, run fetch acpica first" >&2; exit 1; }
    local number=0 agree=0 deferred=0 waiting=0 subject ours theirs result runs=""
    local -a tables
    ours="$(mktemp)" theirs="$(mktemp)"
    while IFS= read -r subject; do
        number=$((number + 1))
        if [[ "$1" == qemu ]]; then
            read -ra tables <<<"$subject"
            "$HARNESS" --namespace-tables "${tables[@]}" >"$ours" 2>&1 || true
            python3 -I "$ROOT/tools/acpi/acpiexec_namespace.py" "$ACPICA_BIN" --tables "${tables[@]}" >"$theirs"
            subject="${tables[0]#"$QEMU_TABLES"/}"
        else
            if ! "$HARNESS" --namespace "$LINUXHW/$subject.bin" >"$ours" 2>&1; then
                echo "not ok $number $subject: the harness failed"
                sed 's/^/    # /' "$ours" | tail -5
                continue
            fi
            runs+="$(grep '^aml run ' "$ours")"$'\n' || true
            if ! grep -q '^aml namespace table_level_failures=0 ' "$ours"; then
                echo "skip $number $subject: $(head -1 "$ours" | cut -d' ' -f3)"
                deferred=$((deferred + 1))
                continue
            fi
            python3 -I "$ROOT/tools/acpi/acpiexec_namespace.py" "$ACPICA_BIN" "$LINUXHW/$subject.bin" >"$theirs"
        fi
        if result="$(python3 -I "$ROOT/tools/acpi/compare_namespace.py" "$ours" "$theirs")"; then
            echo "ok $number $subject: $result"
            agree=$((agree + 1))
            waiting=$((waiting + $(cut -d' ' -f3 <<<"$result")))
        else
            echo "not ok $number $subject"
            sed 's/^/    # /' <<<"$result"
        fi
    done < <(if [[ "$1" == qemu ]]; then qemu_configurations; else reports "$1"; fi)
    rm -f "$ours" "$theirs"
    echo "# namespace: $agree of $((number - deferred)) agree with acpiexec; $deferred fail in table-level code;" \
        "$waiting nodes wait for the interpreter"
    [[ "$1" == qemu ]] || echo "# table-level code: $(grep -o '^aml run statements=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) runs," \
        "$(grep -o ' failures=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) failed, on" \
        "$(grep -c ' failures=[1-9]' <<<"$runs") machines;" \
        "$(grep -o ' declared_not_run=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) declarations made without running"
    [[ "$1" == qemu ]] || echo "# field accesses: at most $(grep -o ' field_accesses_peak=[0-9]*' <<<"$runs" | cut -d= -f2 |
        sort -n | tail -1) in one table load"
    [[ "$1" == qemu ]] || grep '^aml run \(status\|unimplemented\|space\) ' <<<"$runs" | awk '{ split($4, pair, "="); total[$3 " " pair[1]] += pair[2] }
        END { for (key in total) printf "#   %s %u\n", key, total[key] }' | sort -k4,4nr || true
    [[ "$number" -gt 0 && "$agree" -eq "$((number - deferred))" ]]
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

# Compares the directory, the FADT and the MADT the kernel's code reads from each dump of the list $1 with the
# expected ones, then says how many FADTs, registers, CPU slots, I/O APICs, overrides and NMI connections the
# comparison held.
check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 compared="" disassembly="" faults="" machine dump actual expected tables
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
        faults+="$(grep '^aml faults ' <<<"$actual")"$'\n' || true
        actual="$(grep -v '^aml faults ' <<<"$actual")" || true
        expected="$(python3 -I "$ROOT/tools/acpi/expected_directory.py" "$dump")
$(decoded "$machine" | python3 -I "$ROOT/tools/acpi/expected_fadt.py" "$dump" -)
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
    echo "# directory, FADT and MADT: $agree of $number machines agree"
    echo "# compared: $(grep -c '^fadt present=1 ' <<<"$compared") FADTs," \
        "$(grep -c '^fadt [a-z0-9_]* space=[0-9]* width=[0-9]* offset=[0-9]* access=[0-9]* address=0x[1-9A-F]' <<<"$compared") registers present," \
        "$(grep -c '^processor ' <<<"$compared") CPU slots," \
        "$(grep -c '^io_apic ' <<<"$compared") I/O APICs, $(grep -c '^override ' <<<"$compared") overrides," \
        "$(grep -c '^nmi ' <<<"$compared") NMI connections, $(grep -c '^aml opcode ' <<<"$compared") opcode counts," \
        "$(grep -o ' calls=[0-9]*' <<<"$compared" | cut -d= -f2 | paste -sd+ | bc) method calls"
    tables="$(grep -o '^aml tables=[0-9]*' <<<"$compared" | cut -d= -f2 | paste -sd+ | bc)"
    echo "# iASL disassembly: $(grep -c ' iasl agrees ' <<<"$disassembly") tables declare as many objects," \
        "$(grep -c ' iasl differs ' <<<"$disassembly") differ, $(grep -c ' iasl missing ' <<<"$disassembly") have none," \
        "of $tables"
    grep ' iasl differs ' <<<"$disassembly" | sed 's/^/#   /' || true
    echo "# load: $(grep -o 'load_errors=[0-9]*' <<<"$faults" | cut -d= -f2 | paste -sd+ | bc) faults on" \
        "$(grep -c 'load_errors=[1-9]' <<<"$faults") machines; $(grep -o 'unresolved_names=[0-9]*' <<<"$faults" |
            cut -d= -f2 | paste -sd+ | bc) names that resolve to nothing"
    [[ "$(grep -c ' iasl \(agrees\|differs\|missing\) ' <<<"$disassembly")" -eq "$tables" ]] ||
        { echo "# iASL comparison: not every table was compared" >&2; return 1; }
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]]
}

# Prints, for each opcode the needs-path closure of some machine of the list $1, or of some QEMU configuration,
# reaches, the number of machines it reaches it on, and their names when they are five or fewer; fails when a machine
# could not be read, or when decoding met a fault, which cuts a body and the closure with it.
closure_table() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local subject
    local -a tables
    while IFS= read -r subject; do
        if [[ "$1" == qemu ]]; then
            read -ra tables <<<"$subject"
            echo "subject ${tables[0]#"$QEMU_TABLES"/}"
            "$HARNESS" --closure-tables "${tables[@]}" 2>&1 || echo "harness failed"
        else
            echo "subject $subject"
            "$HARNESS" --closure "$LINUXHW/$subject.bin" 2>&1 || echo "harness failed"
        fi
    done < <(if [[ "$1" == qemu ]]; then qemu_configurations; else reports "$1"; fi) |
        awk '
            /^subject / { ++subjects; current = substr($0, 9); next }
            /^harness failed/ { unread[++failures] = current; next }
            /^aml namespace / {
                for (field = 1; field <= NF; ++field) if ($field ~ /^decode_errors=/) faults += substr($field, 15)
            }
            /^aml closure opcode / {
                if (!($4 in reached)) ++count
                machines[$4] = machines[$4] ((reached[$4]++ == 0) ? "" : "; ") current
            }
            /^aml closure entries=/ {
                ++seen
                for (field = 1; field <= NF; ++field) if ($field ~ /^dropped=/) dropped += substr($field, 9)
            }
            END {
                sorter = "sort -k3,3nr -k2,2"
                for (opcode in reached)
                    printf "closure %s %u of %u%s\n", opcode, reached[opcode], subjects,
                        (reached[opcode] <= 5) ? ": " machines[opcode] : "" | sorter
                close(sorter)
                printf "# closure: %u opcodes reached on some machine of %u; %u observations dropped; %u decode faults\n",
                    count, subjects, dropped, faults
                for (index_ = 1; index_ <= failures; ++index_) printf "# not read: %s\n", unread[index_]
                exit (seen != subjects || faults != 0) ? 1 : 0
            }'
}

case "${1:-}" in
    fetch)
        mkdir -p "$CORPUS"
        case "${2:-corpus}" in
            acpica) fetch_acpica ;;
            qemu) fetch_qemu ;;
            uacpi) fetch_uacpi ;;
            *) known_list "${2:-corpus}"; fetch "${2:-corpus}" ;;
        esac
        ;;
    namespace) [[ "${2:-corpus}" == qemu ]] || known_list "${2:-corpus}"; namespace_check "${2:-corpus}" ;;
    closure) [[ "${2:-corpus}" == qemu ]] || known_list "${2:-corpus}"; closure_table "${2:-corpus}" ;;
    check) known_list "${2:-corpus}"; check "${2:-corpus}" ;;
    uacpi) uacpi_check ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
