#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi-corpus.sh fetch [quick|corpus|full|acpica|qemu|uacpi]
       tools/acpi-corpus.sh check [quick|corpus|full]
       tools/acpi-corpus.sh namespace [quick|corpus|full|qemu]
       tools/acpi-corpus.sh closure [quick|corpus|full|qemu]
       tools/acpi-corpus.sh initialize [quick|corpus|full|qemu]
       tools/acpi-corpus.sh idle [quick|corpus|full]
       tools/acpi-corpus.sh load [quick|corpus|full]
       tools/acpi-corpus.sh uacpi
       tools/acpi-corpus.sh module <machine>...
       tools/acpi-corpus.sh ring0 quick|corpus|full|qemu <kernel>

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
         decode with what tools/acpi/aml_census.py counts, once tools/acpi/check_census.py has
         checked that census against hand-made tables; then
         tools/acpi/check_against_iasl.py compares the census's declarations, table by table, with
         the report's iASL disassembly, and lists the tables where they differ. Once fetch acpica has
         built acpiexec, the FADT is also compared with the local FADT ACPICA builds from the same table,
         printed by tools/acpi/acpica_fadt.c, built here against acpiexec's objects, and compared by
         tools/acpi/against_acpica_fadt.py
  namespace  loads each machine of a list, or each QEMU configuration, with `test-acpi --namespace`
         and compares its namespace with the one acpiexec loads from the same tables; a machine whose
         table-level code fails is counted and left out, until the interpreter runs what failed; it also
         sums the duplicate declarations each load refused
  closure  loads each machine of a list, or each QEMU configuration, with `test-acpi --closure`, and
         prints, for each opcode the needs path reaches on some machine, how many machines it reaches
         it on, and which when they are five or fewer: the opcodes the interpreter executes
         (specification §4.1); a machine not read or a decode fault fails it
  initialize  loads each machine of a list, or each QEMU configuration, initialises its namespace with
         `test-acpi --initialize` as the boot does, and sums what the initialisations ran and counted, the
         C-states they published, then the statuses their failures ended with; a machine the harness fails on
         fails it. The SSDTs a dump's firmware loads at run time wait for Load where
         tools/acpi/loaded_tables.py places them, and tools/acpi/presets.txt gives the NVS objects firmware
         fills at boot their values, which a dump lacks (specification §8.4)
  load  initialises each machine of a list whose firmware loads tables at run time, with
         `test-acpi --loaded`, and compares the paths of its namespace with those of the namespace
         acpiexec loads, statically, from the tables it ended with, Load's included; a machine whose
         table-level code fails is counted and left out, as namespace leaves it out
  idle   initialises each machine of a list with `test-acpi --loaded`, its presets given, then compares the
         C-states it publishes for each CPU slot with those tools/acpi/acpiexec_idle.py works out apart from
         the kernel's code: the processor objects and the slots they bind to from acpiexec's namespace and the
         decoded MADT, their states from the _CST acpiexec evaluates after the same handshake, given the
         tables the initialisation ended with and the same presets; a machine where acpiexec stops before an
         evaluation it was given is counted apart
  uacpi  compiles each of uACPI's test cases with the iasl acpica built and evaluates its \MAIN with
         `test-acpi --evaluate` (specification §8.5); a case passes, diverges as
         tools/acpi/uacpi-divergences.list says, is skipped, or waits for what the kernel does not run
         yet; any other result fails
  module  writes the firmware of each machine named, as `test-acpi --write-module` serves it, as one
         Multiboot module in build/acpi-corpus/modules/, and prints its path
  ring0  boots the debug kernel <kernel> on QEMU once per machine of a list, its module given with
         -initrd, runs the ACPI tests with `lpl.test=advanced_configuration_and_power_interface*`,
         and compares the records with those `test-acpi --boot-module` prints on the same module,
         with tools/parity.sh; qemu boots it on q35 and pc with lpl.acpi.tables=print instead, and
         compares with `test-acpi --firmware-log` on the tables it printed; a boot that ends no test run,
         has no telemetry record, or whose telemetry kept a duplicate key, sanitised a character or lost a
         field past a record's cap, fails. The logs of the last run
         stay in build/acpi-corpus/ring0/. BOOT_SECONDS (300) bounds each boot

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
readonly PRESETS="$ROOT/tools/acpi/presets.txt"
readonly ACPICA="$CORPUS/acpica"
readonly ACPICA_URL="https://github.com/acpica/acpica.git"
readonly ACPICA_COMMIT="e85aa3bebaae81ed1e6d163511ac7e1eafe732cf"
readonly ACPICA_BIN="$ACPICA/generate/unix/bin"
readonly FADT_ORACLE="$ROOT/build/acpi-host/acpica-fadt"
readonly QEMU="$CORPUS/qemu"
readonly QEMU_URL="https://github.com/qemu/qemu.git"
readonly QEMU_COMMIT="2d3df8abca265c9bcc9e438d691d561592060998"
readonly QEMU_TABLES="$QEMU/tests/data/acpi/x86"
readonly UACPI="$CORPUS/uacpi"
readonly UACPI_URL="https://github.com/uACPI/uACPI.git"
readonly UACPI_COMMIT="c0edecef62bcac6ce3f783540099190bafadadda"
readonly MODULES="$CORPUS/modules"
readonly RING0_LOGS="$CORPUS/ring0"
readonly SUITES="advanced_configuration_and_power_interface"

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
    local number=0 agree=0 deferred=0 waiting=0 duplicates=0 duplicated=0 count subject ours theirs result runs=""
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
        fi
        count="$(grep -o ' duplicates=[0-9]*' "$ours" | head -1 | cut -d= -f2)"
        if [[ "${count:-0}" != 0 ]]; then
            duplicates=$((duplicates + count))
            duplicated=$((duplicated + 1))
        fi
        if [[ "$1" != qemu ]]; then
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
    echo "# duplicates at load: $duplicates on $duplicated of $number"
    [[ "$1" == qemu ]] || echo "# table-level code: $(grep -o '^aml run statements=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) runs," \
        "$(grep -o ' failures=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) failed, on" \
        "$(grep -c ' failures=[1-9]' <<<"$runs") machines;" \
        "$(grep -o ' declared_not_run=[0-9]*' <<<"$runs" | cut -d= -f2 | paste -sd+ | bc) declarations made without running"
    [[ "$1" == qemu ]] || echo "# field accesses: at most $(grep -o ' field_accesses_peak=[0-9]*' <<<"$runs" | cut -d= -f2 |
        sort -n | tail -1) in one table load"
    [[ "$1" == qemu ]] || echo "# PCI_Config: $(pci_total attachments "$runs") regions attached through" \
        "$(pci_total internal_evaluations "$runs") internal evaluations, $(pci_total internal_failures "$runs") of" \
        "them failed; $(pci_total protected_writes "$runs")" \
        "protected writes dropped, $(pci_total lost_accesses "$runs") accesses to unreachable functions," \
        "$(pci_total ecam_accesses "$runs") that need ECAM on $(grep -c '^aml run pci .* ecam_accesses=[1-9]' <<<"$runs") machines"
    [[ "$1" == qemu ]] || grep '^aml run \(status\|unimplemented\|space\) ' <<<"$runs" | awk '{ split($4, pair, "="); total[$3 " " pair[1]] += pair[2] }
        END { for (key in total) printf "#   %s %u\n", key, total[key] }' | sort -k4,4nr || true
    [[ "$number" -gt 0 && "$agree" -eq "$((number - deferred))" ]]
}

# Initialises the namespace of each machine of the list $1, or each QEMU configuration, with
# `test-acpi --initialize`, and sums what the initialisations ran and counted; a machine the harness fails
# on fails the run.
initialize_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 failed=0 subject output counts="" key
    local -a tables
    output="$(mktemp)"
    while IFS= read -r subject; do
        number=$((number + 1))
        if [[ "$1" == qemu ]]; then
            read -ra tables <<<"$subject"
            subject="${tables[0]#"$QEMU_TABLES"/}"
            "$HARNESS" --initialize-tables "${tables[@]}" >"$output" 2>&1 || true
        else
            "$HARNESS" --initialize "$LINUXHW/$subject.bin" "$PRESETS" >"$output" 2>&1 || true
        fi
        if ! grep -q '^aml init initializations=' "$output"; then
            echo "not ok $number $subject: the harness failed"
            sed 's/^/    # /' "$output" | tail -5
            failed=$((failed + 1))
            continue
        fi
        echo "ok $number $subject: $(grep '^aml init initializations=' "$output" | cut -d' ' -f3-)"
        counts+="$(grep -E '^aml (init|idle|load) |^aml run tables ' "$output")"$'\n'
    done < <(if [[ "$1" == qemu ]]; then qemu_configurations; else reports "$1"; fi)
    rm -f "$output"
    echo -n "# initialization of $((number - failed)) of $number:"
    for key in initializations initialization_failed registrations region_registration_failed devices \
        status_evaluations status_failed status_invalid pic_mode_deferred processor_capabilities \
        processor_capabilities_legacy processor_capabilities_refused processor_capabilities_failed processors_unbound \
        late_initializations; do
        echo -n " $key=$(grep '^aml init initializations=' <<<"$counts" | grep -o " $key=[0-9]*" | cut -d= -f2 |
            paste -sd+ | bc)"
    done
    echo
    echo "# tables Load places: $(total_of placed "$counts"), on $(grep -c '^aml load placed=[1-9]' <<<"$counts")" \
        "machines; added at run time: $(total_of loaded "$counts"), on $(grep -c '^aml run tables loaded=[1-9]' <<<"$counts")" \
        "machines; refused as loaded already: $(total_of duplicates "$counts"); LoadTable found none:" \
        "$(total_of missing "$counts"); presets given: $(total_of presets "$counts")"
    echo "# platform _OSC, machines per answer:" \
        $(grep -o ' platform_capabilities=[a-z]*' <<<"$counts" | cut -d= -f2 | sort | uniq -c | awk '{ printf " %s=%s", $2, $1 }')
    echo "# unimplemented opcodes the initialisation reached:" \
        $(grep '^aml init unimplemented ' <<<"$counts" | awk '{ split($4, pair, "="); total[pair[1]] += pair[2] }
            END { for (key in total) printf " %s=%u", key, total[key] }')
    echo -n "# C-states of"
    for key in slots published states skipped processor_idle processor_idle_failed processor_idle_absent \
        processor_idle_unbound idle_states_dropped processor_idle_changes; do
        echo -n " $key=$(grep '^aml idle control=' <<<"$counts" | grep -o " $key=[0-9]*" | cut -d= -f2 | paste -sd+ | bc)"
    done
    echo
    echo "# CST_CNT, machines per outcome:" \
        $(grep -o '^aml idle control=[a-z]*' <<<"$counts" | cut -d= -f2 | sort | uniq -c | awk '{ printf " %s=%s", $2, $1 }')
    echo "# machines with a failure: $(grep -c '^aml init first_failure ' <<<"$counts")"
    grep '^aml init failed ' <<<"$counts" | awk '{ split($4, pair, "="); total[pair[1]] += pair[2] }
        END { for (key in total) printf "#   %s %u\n", key, total[key] }' | sort -k3,3nr || true
    [[ "$number" -gt 0 && "$failed" -eq 0 ]]
}

# Initialises each machine of the list $1 that places tables for Load, and compares the paths of its
# namespace with those acpiexec loads statically from the tables the initialisation ended with.
load_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    [[ -x "$ACPICA_BIN/acpiexec" ]] || { echo "load: no acpiexec, run fetch acpica first" >&2; exit 1; }
    local number=0 agree=0 deferred=0 placed=0 loaded=0 subject ours theirs tables result counts
    ours="$(mktemp)" theirs="$(mktemp)" tables="$(mktemp -d)"
    while IFS= read -r subject; do
        rm -f "$tables"/*.dat
        if ! "$HARNESS" --loaded "$LINUXHW/$subject.bin" "$PRESETS" "$tables" >"$ours" 2>&1; then
            number=$((number + 1))
            echo "not ok $number $subject: the harness failed"
            sed 's/^/    # /' "$ours" | tail -5
            continue
        fi
        grep -q '^aml load placed=[1-9]' "$ours" || continue
        number=$((number + 1))
        counts="$(grep '^aml load \|^aml run tables ' "$ours")"
        placed=$((placed + $(total_of placed "$counts")))
        loaded=$((loaded + $(total_of loaded "$counts")))
        if ! grep -q '^aml namespace table_level_failures=0 ' "$ours"; then
            echo "skip $number $subject: $(head -2 "$ours" | tail -1 | cut -d' ' -f3)"
            deferred=$((deferred + 1))
            continue
        fi
        python3 -I "$ROOT/tools/acpi/acpiexec_namespace.py" "$ACPICA_BIN" --tables "$tables"/*.dat >"$theirs"
        if result="$(python3 -I "$ROOT/tools/acpi/compare_namespace.py" --paths "$ours" "$theirs")"; then
            echo "ok $number $subject: $(total_of loaded "$counts") of $(total_of placed "$counts") tables loaded;" \
                "$result"
            agree=$((agree + 1))
        else
            echo "not ok $number $subject"
            sed 's/^/    # /' <<<"$result"
        fi
    done < <(reports "$1")
    rm -rf "$ours" "$theirs" "$tables"
    echo "# load: $agree of $((number - deferred)) machines that place tables for Load agree with acpiexec;" \
        "$deferred fail in table-level code; $loaded of the $placed tables placed were loaded"
    [[ "$number" -gt 0 && "$agree" -eq "$((number - deferred))" ]]
}

# Prints the sum of the counter $1 over the lines $2.
total_of() {
    { grep -o " $1=[0-9]*" <<<"$2" || true; echo " $1=0"; } | cut -d= -f2 | paste -sd+ | bc
}

# Compares the C-states each machine of the list $1 publishes with those tools/acpi/acpiexec_idle.py works out apart from
# the kernel's code: the processor objects and their CPU slots from acpiexec's namespace and the decoded MADT, their
# states from the _CST acpiexec evaluates after the same handshake (specification §8.6).
idle_check() {
    [[ -x "$ACPICA_BIN/acpiexec" ]] || { echo "idle: no acpiexec, run fetch acpica first" >&2; exit 1; }
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 none=0 stopped=0 slots=0 states=0 subject ours theirs madt tables
    ours="$(mktemp)" theirs="$(mktemp)" madt="$(mktemp)" tables="$(mktemp -d)"
    while IFS= read -r subject; do
        number=$((number + 1))
        if ! has_decoded "$subject"; then
            echo "not ok $number $subject: no decoded report, run fetch first"
            continue
        fi
        rm -f "$tables"/*.dat
        "$HARNESS" --loaded "$LINUXHW/$subject.bin" "$PRESETS" "$tables" 2>&1 | grep -E '^aml idle (slot|state) ' >"$ours" ||
            true
        decoded "$subject" | python3 -I "$ROOT/tools/acpi/expected_madt.py" "$LINUXHW/$subject.bin" - >"$madt" || true
        python3 -I "$ROOT/tools/acpi/acpiexec_idle.py" "$ACPICA_BIN" "$madt" "$PRESETS" "$tables"/*.dat >"$theirs" || true
        if grep -q '^aml idle unevaluated ' "$theirs"; then
            echo "ok $number $subject # SKIP acpiexec stops before $(cut -d' ' -f4 "$theirs")"
            stopped=$((stopped + 1))
        elif cmp -s "$ours" "$theirs"; then
            agree=$((agree + 1))
            [[ -s "$ours" ]] || none=$((none + 1))
            slots=$((slots + $(grep -c '^aml idle slot ' "$ours" || true)))
            states=$((states + $(grep -c '^aml idle state ' "$ours" || true)))
            echo "ok $number $subject"
        else
            echo "not ok $number $subject"
            diff "$ours" "$theirs" | head -8 | sed 's/^/    # /' || true
        fi
    done < <(reports "$1")
    rm -rf "$ours" "$theirs" "$madt" "$tables"
    echo "# idle: $agree of $((number - stopped)) agree with acpiexec, $slots CPU slots and $states states, $none" \
        "publishing none on both sides; acpiexec stops before the evaluations it was given on $stopped"
    [[ "$agree" -eq $((number - stopped)) ]]
}

# Prints the sum of the PCI_Config counter $1 over the `aml run pci` lines of the runs $2.
pci_total() {
    grep '^aml run pci ' <<<"$2" | grep -o " $1=[0-9]*" | cut -d= -f2 | paste -sd+ | bc
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

# Builds tools/acpi/acpica_fadt.c, which prints the local FADT ACPICA builds from a table, against the objects of the
# acpiexec fetch acpica built, but acpiexec's own. Returns 2 when that acpiexec was not built, and 1, after saying so,
# when the build fails.
build_fadt_oracle() {
    local library="$ROOT/build/acpi-host/libacpica-exec.a" objects=()
    [[ -x "$ACPICA_BIN/acpiexec" ]] || return 2
    [[ "$FADT_ORACLE" -nt "$ROOT/tools/acpi/acpica_fadt.c" && "$FADT_ORACLE" -nt "$ACPICA_BIN/acpiexec" ]] && return 0
    mapfile -t objects < <(find "$ACPICA/generate/unix/acpiexec/obj" -name '*.o' ! -name 'ae*.o')
    mkdir -p "$ROOT/build/acpi-host"
    rm -f "$library" "$FADT_ORACLE"
    if ! ar rcs "$library" "${objects[@]}" ||
        ! cc -std=c99 -O2 -Wall -Wextra -Werror -D_LINUX -D_GNU_SOURCE -DACPI_EXEC_APP -I"$ACPICA/source/include" \
            -I"$ACPICA/source/tools/acpiexec" -o "$FADT_ORACLE" "$ROOT/tools/acpi/acpica_fadt.c" "$library" \
            -lpthread -lrt; then
        echo "# FADT against ACPICA's: tools/acpi/acpica_fadt.c does not build against $ACPICA" >&2
        return 1
    fi
}

# Prints, for the dump $1 and the harness's lines $2, whether its FADT agrees with the local FADT ACPICA builds from
# the same table, then, when it does not, the fields that differ and what the program printed on its error output.
against_acpica_fadt() {
    local errors="$ROOT/build/acpi-host/acpica-fadt.errors" lines verdict
    lines="$(python3 -I "$ROOT/tools/acpi/fadt_bytes.py" "$1" | "$FADT_ORACLE" 2>"$errors")" || true
    verdict="$(python3 -I "$ROOT/tools/acpi/against_acpica_fadt.py" <(grep '^fadt ' <<<"$2") <(echo "$lines"))"
    echo "$verdict"
    [[ "$verdict" == "fadt acpica agrees"* ]] || sed 's/^/acpica-fadt: /' "$errors"
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
    local number=0 agree=0 compared="" disassembly="" faults="" machine dump actual expected tables census
    local oracle=0 acpica="" verdict
    build_fadt_oracle || oracle=$?
    [[ "$oracle" -ne 1 ]] || return 1
    if ! census="$(python3 -I "$ROOT/tools/acpi/check_census.py")"; then
        grep -v '^ok ' <<<"$census"
        return 1
    fi
    tail -1 <<<"$census"
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
        verdict="fadt acpica agrees"
        if [[ "$oracle" -eq 0 ]]; then
            verdict="$(against_acpica_fadt "$dump" "$actual")"
            acpica+="$(head -1 <<<"$verdict")"$'\n'
        fi
        [[ "$actual" == "$expected" ]] && agree=$((agree + 1))
        if [[ "$actual" == "$expected" && "$verdict" == "fadt acpica agrees"* ]]; then
            echo "ok $number $machine"
        else
            echo "not ok $number $machine"
            diff <(echo "$expected") <(echo "$actual") | sed 's/^/    # /' || true
            [[ "$verdict" == "fadt acpica agrees"* ]] || sed 's/^ */    # /' <<<"$verdict"
        fi
    done < <(reports "$1")
    echo "# directory, FADT and MADT: $agree of $number machines agree"
    if [[ "$oracle" -eq 0 ]]; then
        echo "# FADT against ACPICA's: $(grep -c '^fadt acpica agrees' <<<"$acpica") of $number agree, the fields" \
            "ACPI says to ignore left out of the $(grep -c ' hardware-reduced$' <<<"$acpica") hardware-reduced ones"
    else
        echo "# FADT against ACPICA's: not compared, run fetch acpica first"
    fi
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
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]] &&
        [[ "$oracle" -eq 2 || "$(grep -c '^fadt acpica agrees' <<<"$acpica")" -eq "$number" ]]
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

# Prints the file name of the module of the machine $1: its path, each character but a letter, a digit,
# a dot or a dash made an underscore, so that QEMU's -initrd takes it whole.
module_name() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9.-' '_'
    printf '.acpi\n'
}

# Writes the module of each machine named, and prints its path.
modules() {
    [[ "$#" -gt 0 ]] || { usage >&2; exit 2; }
    (cd "$ROOT" && xmake build -q test-acpi)
    mkdir -p "$MODULES"
    local machine module
    for machine in "$@"; do
        module="$MODULES/$(module_name "$machine")"
        "$HARNESS" --write-module "$LINUXHW/$machine.bin" "$module" || exit 1
        echo "$module"
    done
}

# Boots the kernel $1 on the QEMU machine $2, its serial port written to the log $3, with the boot line $4
# and the module $5 when there is one, until its tests print their totals or BOOT_SECONDS pass. Both are
# copied under a short path first: QEMU puts the kernel's path at the head of the boot line, which the
# kernel cuts at 255 characters (#553), and takes a space or a comma in a module's path for a separator.
boot() {
    local kernel="$1" machine="$2" log="$3" line="$4" module="${5:-}" short pid
    local -a initrd=()
    short="$(mktemp -d)"
    cp "$kernel" "$short/k"
    if [[ -n "$module" ]]; then
        cp "$module" "$short/m.acpi"
        initrd=(-initrd "$short/m.acpi")
    fi
    : >"$log"
    qemu-system-i386 -machine "$machine" -m 256 -kernel "$short/k" "${initrd[@]}" -append "$line" \
        -serial "file:$log" -display none -no-reboot >/dev/null 2>&1 &
    pid=$!
    trap 'kill "$pid" 2>/dev/null; rm -rf "$short"; exit 130' INT TERM
    for _ in $(seq "${BOOT_SECONDS:-300}"); do
        grep -qF '# Totals:' "$log" && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    trap - INT TERM
    rm -rf "$short"
}

# Compares the host log $1 with the serial log $2, and prints `ok` or `not ok` for the subject $3, numbered
# $4, with what differs. tools/parity.sh compares only the tests the host ran, so the test $5, which reads the
# subject's tables, must have run on the host; ring 0 is held to it by lpl.test.require.
compare_ring0() {
    local result
    if ! grep -qaE '^# Totals: ' "$2"; then
        echo "not ok $4 $3: the boot printed no '# Totals:': it hung or crashed in its tests"
        return 1
    fi
    if ! grep -qaE '^\[LPLTLM\] telemetry .* duplicate_keys=0 sanitised_characters=0 dropped_fields=0$' "$2"; then
        echo "not ok $4 $3: the boot's telemetry is missing, or kept a duplicate key, a character it had to sanitise or a"
        echo "    # field past a record's cap: $(grep -aE '^\[LPLTLM\] telemetry ' "$2" || echo 'no telemetry record')"
        return 1
    fi
    if ! grep -qE "^    ok [0-9]+ $5\$" "$1"; then
        echo "not ok $4 $3: $5 did not run on the host"
        grep -E "^test-acpi: | $5( |\$)" "$1" | sed 's/^/    # /'
        return 1
    fi
    if result="$("$ROOT/tools/parity.sh" "$1" "$2")"; then
        echo "ok $4 $3: $(tail -1 <<<"$result" | sed 's/^# //')"
        return 0
    fi
    echo "not ok $4 $3"
    grep -v '^ok ' <<<"$result" | sed 's/^/    # /'
    return 1
}

# Boots the debug kernel $2 with the module of each machine of the list $1, and compares its records with the
# host harness's on the same module; with qemu, boots it on q35 and pc and compares the tables it printed.
ring0() {
    command -v qemu-system-i386 >/dev/null || { echo "ring0: no qemu-system-i386" >&2; exit 1; }
    [[ -f "$2" ]] || { echo "ring0: $2: no kernel" >&2; exit 1; }
    grep -qaF 'KTAP version 1' "$2" || { echo "ring0: $2: a kernel without tests, build a debug one" >&2; exit 1; }
    (cd "$ROOT" && xmake build -q test-acpi)
    mkdir -p "$RING0_LOGS"
    local number=0 agree=0 subject logs test
    while IFS= read -r subject; do
        number=$((number + 1))
        logs="$RING0_LOGS/$(module_name "$subject")"
        mkdir -p "$logs"
        if [[ "$1" == qemu ]]; then
            test=the_booted_machine_loads
            boot "$2" "$subject" "$logs/serial.log" \
                "lpl.acpi.tables=print lpl.test=$SUITES* lpl.test.require=${SUITES}_corpus.$test"
            "$HARNESS" --firmware-log "$logs/serial.log" >"$logs/host.log" 2>&1 || true
        else
            test=the_boot_module_machine_loads
            if ! "$HARNESS" --write-module "$LINUXHW/$subject.bin" "$logs/machine.acpi" 2>"$logs/host.log"; then
                echo "not ok $number $subject: $(cat "$logs/host.log")"
                continue
            fi
            "$HARNESS" --boot-module "$logs/machine.acpi" >"$logs/host.log" 2>&1 || true
            boot "$2" q35 "$logs/serial.log" "lpl.test=$SUITES* lpl.test.require=${SUITES}_corpus.$test" \
                "$logs/machine.acpi"
        fi
        compare_ring0 "$logs/host.log" "$logs/serial.log" "$subject" "$number" "$test" && agree=$((agree + 1))
    done < <(if [[ "$1" == qemu ]]; then printf 'q35\npc\n'; else reports "$1"; fi)
    echo "# ring 0: $agree of $number agree with the host"
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]]
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
    initialize) [[ "${2:-corpus}" == qemu ]] || known_list "${2:-corpus}"; initialize_check "${2:-corpus}" ;;
    idle) known_list "${2:-corpus}"; idle_check "${2:-corpus}" ;;
    load) known_list "${2:-corpus}"; load_check "${2:-corpus}" ;;
    check) known_list "${2:-corpus}"; check "${2:-corpus}" ;;
    uacpi) uacpi_check ;;
    module) shift; modules "$@" ;;
    ring0)
        [[ "$#" -eq 3 ]] || { usage >&2; exit 2; }
        [[ "$2" == qemu ]] || known_list "$2"
        ring0 "$2" "$3"
        ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
