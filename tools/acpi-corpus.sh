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
       tools/acpi-corpus.sh slack [quick|corpus|full]
       tools/acpi-corpus.sh load [quick|corpus|full]
       tools/acpi-corpus.sh differential [quick|corpus|full] [<mutants>]
       tools/acpi-corpus.sh kept [quick|corpus|full] [<mutants>]
       tools/acpi-corpus.sh predecoding [quick|corpus|full]
       tools/acpi-corpus.sh records-cost [corpus|full]
       tools/acpi-corpus.sh uacpi
       tools/acpi-corpus.sh module <machine>...
       tools/acpi-corpus.sh module qemu q35|pc <kernel>
       tools/acpi-corpus.sh ring0 quick|corpus|full|qemu <kernel>

The firmware of recorded machines, which the ACPI layer is checked on. linuxhw/ACPI is fetched at a
pinned commit into build/acpi-corpus/, never committed and never put in an image.

  fetch  adds the dumps of a list to those already fetched: quick (tools/acpi/quick.list), corpus
         (tools/acpi/corpus.list, the default) or full (every report with a dump, written to
         build/acpi-corpus/full.list); each report's decoded tables come too, kept in the clone's
         objects and never checked out
         acpica builds iasl, acpiexec and acpixtract from ACPICA at a pinned commit, unchanged and cleaned
         of what git does not track, in the sandbox of tools/acpi/sandbox.sh, which needs bubblewrap
         (apt install bubblewrap); qemu fetches the reference tables of QEMU at a pinned release, its 47
         x86 configurations; uacpi fetches uACPI's test cases at a pinned commit
  check  walks each machine of a list with `test-acpi --machine`, built here, and compares its
         directory with what tools/acpi/expected_directory.py reads from the same dump, its FADT
         and its MADT with what tools/acpi/expected_fadt.py and tools/acpi/expected_madt.py read
         from the report's decoded tables, and the opcodes and method calls its definition blocks
         decode with what tools/acpi/aml_census.py counts, once tools/acpi/check_census.py has
         checked that census against hand-made tables, and fails on a data object a Name kept in its
         table that the check of kept data finds would not build (`aml faults data_faults=`); then
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
         (specification §4.1); then each one the run list leaves out where the path reaches it, in a method
         body or in table-level code, with the machines it is reached on. Such an opcode fails it, and so do a
         machine not read and a decode fault
  initialize  loads each machine of a list, or each QEMU configuration, initialises its namespace with
         `test-acpi --initialize` as the boot does, a recorded machine put in ACPI mode first (step 0), and sums
         how step 0 ended, what the initialisations ran and counted, the C-states they published, the statuses
         their failures ended with, then the opcodes, region spaces and AccessTypes the needs path executed,
         each with how many machines executed it; a machine the harness fails on fails it, and so does one
         whose run prints no outcome of step 0, whose step 0 did not run on the FADT read or found no SCI_EN
         set, whose CST_CNT step 8 skipped though step 0 switched it, or whose table-level code or
         initialisation executes an opcode the interpreter does not run (specification §4.1). The SSDTs a
         dump's firmware loads at run time wait for Load where tools/acpi/loaded_tables.py places them, and
         tools/acpi/presets.txt gives the NVS objects firmware fills at boot their values, which a dump lacks
         (specification §8.4)
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
  slack  initialises each machine of a list with `test-acpi --loaded`, its presets given; then
         tools/acpi/acpiexec_slack.py has acpiexec evaluate the needs path a host can ask it for, in the
         order of §3.8, on the tables the initialisation ended with, once as acpiexec runs and once in its
         slack mode (`acpiexec -es`); the mode lists the machines whose load or results differ, those that
         rely on slack behaviour, which reopen the implicit-return and uninitialised-operand rules
         (specification §8.6): one that tools/acpi/slack-decided.list names is decided by the section it
         gives, and one it does not name fails the run, as does one it names that does not differ; an _INI,
         a _REG, a _PDC and a method that reads Timer compare on status only, and a machine where acpiexec
         stops before the same evaluation in both runs is counted apart
  differential  loads each machine of a list twice, with the load's shortcuts and with --general-load,
         which takes none (specification §3.6), and compares what `test-acpi --namespace` and
         `test-acpi --initialize` print each way, line for line, what they write to stderr and how they exit: the
         namespace, the faults, the counts and the initialisation, the objects allocated and freed apart, which
         the builder of kept data moves when it gives back what it built from bytes it then hands to the general
         data decoder, the namespace's lookups and probes apart, which the shortcuts save, and the kept data the
         builder of kept data built apart, which only the shortcuts build; with <mutants>, also that many
         copies of each machine whose DSDT and SSDTs have bytes changed at random (`--mutate`), each seeded by its
         number. A load that fails the same way both ways agrees, and is listed with the first line the harness
         wrote to stderr; a run where every load fails compares nothing, and fails
  kept   loads each machine of a list, and <mutants> copies of each whose bytes `--mutate` changes, with
         `test-acpi --read-kept --namespace`, which ends by building every data object the load still keeps in
         its tables (specification §3.6, O20), and checks that the objects the check of kept data counted in
         data_faults are those the builds leave Uninitialized or waiting: as many, on the same nodes (a signature
         of their indices), every build possible (none the heap refused). A load where they differ fails, and so
         does one that prints no `aml kept` line
  predecoding  initialises each machine of a list with `test-acpi --loaded` twice, the statements of the
         loops that loop run from records, then with `--predecoding off`, and compares every line the two
         print, the namespace and `aml run namespace` (lookups, probes and back-edges) included, but `aml
         run predecoding`, whose counts it sums: the statements records ran and handed back, per reason
         (specification §3.6, §8.4); a machine whose two runs differ fails it, and so does a name a record read
         that found another node than the one the record checked (`stale_names`)
  records-cost  initialises each machine of a list with `test-acpi-cost --initialize`, the harness `xmake build
         test-acpi-cost` builds without sanitizers, keeps those whose loops ask the records anything (`aml run
         predecoding` counts a statement run or handed back), and counts the instructions each executes with records
         and with `--predecoding off`, under callgrind; it prints one line per machine and the totals. A machine fails
         it when it costs more with records than without by more than RECORDS_TOLERANCE percent (0.5), when its two
         runs print different lines but `aml run predecoding`, when the harness prints no count of the records on it
         or exits other than 0 under callgrind, or when callgrind counts nothing; so does a list on which the harness
         prints no count, or where no machine asks the records anything. It needs valgrind (`apt install valgrind`),
         or the one VALGRIND names; quick is no list for it, none of its machines asking the records anything
  uacpi  compiles each of uACPI's test cases with the iasl acpica built, in the sandbox, and evaluates its
         \MAIN with `test-acpi --evaluate` (specification §8.5); a case passes, diverges as
         tools/acpi/uacpi-divergences.list says, is skipped, or waits for what the kernel does not run
         yet; any other result fails
  module  writes the firmware of each machine named, as `test-acpi --write-module` serves it, as one
         Multiboot module in build/acpi-corpus/modules/, and prints its path; qemu boots <kernel> on that
         QEMU machine as tools/instructions.sh boots it, its tables printed, and writes those tables, as
         `test-acpi --write-printed-module` serves them, as build/acpi-corpus/modules/q35.acpi or pc.acpi:
         the same tables a release kernel loads in its windows, given to a debug kernel's corpus tests
  ring0  boots the debug kernel <kernel> on QEMU once per machine of a list, its module given with
         -initrd, runs the ACPI tests with `lpl.test=advanced_configuration_and_power_interface*`, the
         two that load the module required, the debug kernel's path and the release kernel's,
         and compares the records with those `test-acpi --boot-module` prints on the same module,
         with tools/parity.sh; qemu boots it on q35 and pc with lpl.acpi.tables=print instead, and
         compares with `test-acpi --firmware-log` on the tables it printed; a boot that ends no test run,
         has no telemetry record, or whose telemetry kept a duplicate key, sanitised a character or lost a
         field past a record's cap, fails. The logs of the last run
         stay in build/acpi-corpus/ring0/. BOOT_SECONDS (300) bounds each boot

Exit status: 0 when every machine agrees, 1 when one does not or its dump or decoded tables are
missing, 2 on a usage error. records-cost also exits 1 when valgrind is missing or xmake cannot build its
harness, and 2 when RECORDS_TOLERANCE is no decimal number. Every mode also exits 1 when a sanitizer stopped
the harness, whatever the mode made of that run, and lists the arguments of the runs it stopped.
USAGE
}

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly CORPUS="$ROOT/build/acpi-corpus"
readonly LINUXHW="$CORPUS/linuxhw"
readonly LINUXHW_URL="https://github.com/linuxhw/ACPI.git"
readonly LINUXHW_COMMIT="d6d30c6ef6f45506567f25a302c64f009d8f5f4e"
readonly HARNESS_BINARY="$ROOT/build/acpi-host/test-acpi"
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
readonly HARNESS_WORK="$(mktemp -d)"
readonly HARNESS="$HARNESS_WORK/test-acpi"
readonly SANITIZER_STOP=86
readonly SANITIZER_STOPS="$HARNESS_WORK/stops"
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}exitcode=$SANITIZER_STOP"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}exitcode=$SANITIZER_STOP"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS:}exitcode=$SANITIZER_STOP"
EXIT_CLEANUP=()

# Writes the script every mode runs as HARNESS: it runs the harness, which exits 0, 1 or 2 on its own, and notes
# the arguments of a run that exited with SANITIZER_STOP, the status the options above give a sanitizer's stop.
write_harness() {
    cat >"$HARNESS" <<WRAPPER
#!/usr/bin/env bash
$(printf '%q' "$HARNESS_BINARY") "\$@"
status=\$?
if [[ "\$status" -eq $SANITIZER_STOP ]]; then
    printf '%s\n' "\${*@Q}" >>$(printf '%q' "$SANITIZER_STOPS")
fi
exit "\$status"
WRAPPER
    chmod +x "$HARNESS"
}

# At exit, removes what the run made to work in, and fails a run in which a sanitizer stopped the harness, with
# the arguments of the first runs it stopped, quoted, which name the machine.
sanitizer_stops_exit() {
    local status=$?
    if [[ -s "$SANITIZER_STOPS" ]]; then
        echo "not ok: a sanitizer stopped the harness $(wc -l <"$SANITIZER_STOPS") time(s), on these arguments:"
        head -20 "$SANITIZER_STOPS" | sed 's/^/    # /'
        status=1
    fi
    rm -rf "$HARNESS_WORK" "${EXIT_CLEANUP[@]}"
    exit "$status"
}
trap sanitizer_stops_exit EXIT
write_harness

# Refuses, with the usage, a list that is not quick, corpus or full.
known_list() {
    case "$1" in
        quick | corpus | full) ;;
        *) usage >&2; exit 2 ;;
    esac
}

# Clones ACPICA at the pinned commit, its blobs fetched as the checkout needs them, and builds the three
# tools the oracles use in the sandbox of tools/acpi/sandbox.sh (specification §8.6): once the sources are
# fetched, the build reaches no network and writes nowhere but in the checkout. A checkout with local changes is
# refused, since the oracles are built from the pinned sources alone, and everything git does not track in it, a
# build made before included, is removed before the build.
fetch_acpica() {
    if [[ ! -d "$ACPICA/.git" ]]; then
        git clone -q --filter=blob:none --no-checkout "$ACPICA_URL" "$ACPICA"
    fi
    git -C "$ACPICA" fetch -q --depth 1 origin "$ACPICA_COMMIT"
    git -C "$ACPICA" checkout -q --detach "$ACPICA_COMMIT"
    if ! git -C "$ACPICA" diff --quiet HEAD; then
        echo "fetch: ${ACPICA#"$ROOT"/} is changed: restore it, since ACPICA is built from its pinned sources" >&2
        exit 1
    fi
    git -C "$ACPICA" clean -fdxq
    (cd "$ROOT" && "$ROOT/tools/acpi/sandbox.sh" "$ACPICA" make -C "$ACPICA" -s -j"$(nproc)" iasl acpiexec acpixtract \
        >/dev/null)
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

# Prints what the initialisation run $1 does wrong at step 0 or at step 8's write of CST_CNT, and nothing when it
# does nothing wrong: it prints no outcome of step 0, step 0 did not run though a FADT was read (fadt_revision is 0
# without one, which §3.8 switches nothing for), the simulated firmware never answered ACPI_ENABLE, or step 8
# skipped the CST_CNT of a platform step 0 switched, whose SMI_CMD the switch showed writable.
step_zero_fault() {
    if ! grep -q '^aml mode outcome=' "$1"; then
        echo "the run printed no outcome of step 0"
    elif grep -q '^aml mode outcome=not-run .* fadt_revision=[1-9]' "$1"; then
        echo "step 0 did not run on the machine's FADT"
    elif grep -q '^aml mode outcome=failed ' "$1"; then
        echo "step 0 wrote ACPI_ENABLE, and the simulated firmware never set SCI_EN"
    elif grep -q '^aml mode outcome=switched ' "$1" && grep -q '^aml idle control=skipped ' "$1"; then
        echo "step 0 switched the platform, and step 8 skipped its CST_CNT"
    fi
}

# Initialises the namespace of each machine of the list $1, or each QEMU configuration, with
# `test-acpi --initialize`, and sums how step 0 ended, what the initialisations ran and counted, then the opcodes,
# region spaces and AccessTypes the needs path executed; a machine the harness fails on fails the run, and so does
# one whose step 0 or CST_CNT step_zero_fault finds wrong, or whose table-level code or initialisation executes an
# opcode the interpreter does not (specification §4.1).
initialize_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 failed=0 subject output counts="" modes="" executed="" key fault
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
        fault="$(step_zero_fault "$output")"
        if [[ -n "$fault" ]]; then
            echo "not ok $number $subject: $fault"
            failed=$((failed + 1))
            continue
        fi
        executed+="$(grep -E '^aml run (executed|unimplemented) ' "$output" || true)"$'\n'
        if grep -q '^aml run unimplemented ' "$output"; then
            echo "not ok $number $subject: the needs path executes what the interpreter does not:" \
                $(grep '^aml run unimplemented ' "$output" | cut -d' ' -f4)
            failed=$((failed + 1))
            continue
        fi
        modes+="$(grep '^aml mode ' "$output")"$'\n'
        counts+="$(grep -E '^aml (init|idle|load) |^aml run tables ' "$output")"$'\n'
        echo "ok $number $subject: $(grep '^aml init initializations=' "$output" | cut -d' ' -f3-)"
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
    echo "# unimplemented opcodes the needs path executed:" \
        $(grep '^aml run unimplemented ' <<<"$executed" | awk '{ split($4, pair, "="); total[pair[1]] += pair[2] }
            END { for (key in total) printf " %s=%u", key, total[key] }')
    for key in opcode space access; do
        echo "# executed, per $key, over every machine the harness read:" \
            $(grep "^aml run executed $key " <<<"$executed" | awk '{ split($5, pair, "="); total[pair[1]] += pair[2]
                    machines[pair[1]] += 1 }
                END { for (key in total) printf "%u %s=%u(%u)\n", total[key], key, total[key], machines[key] }' |
                sort -k1,1nr | cut -d' ' -f2)
    done
    echo -n "# C-states of"
    for key in slots published states skipped processor_idle processor_idle_failed processor_idle_absent \
        processor_idle_unbound idle_states_dropped processor_idle_changes; do
        echo -n " $key=$(grep '^aml idle control=' <<<"$counts" | grep -o " $key=[0-9]*" | cut -d= -f2 | paste -sd+ | bc)"
    done
    echo
    echo "# step 0, machines per outcome:" \
        $(grep -o '^aml mode outcome=[a-z-]*' <<<"$modes" | cut -d= -f2 | sort | uniq -c | awk '{ printf " %s=%s", $2, $1 }')
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

# Loads each machine of the list $1 with the load's shortcuts and without (specification §3.6), and compares what
# the namespace and the initialisation views print each way, the objects the heap allocated and freed apart, which the
# builder of kept data (C4) moves when it gives back what it built from bytes it then hands to the general data decoder,
# what the harness writes to stderr, a sanitizer's report included, its process numbers and addresses apart, and how it
# exits; with $2 mutants, also that many copies of each machine whose DSDT and SSDTs have bytes changed at random,
# seeded by the copy's number. A load that fails the same way both ways agrees, and is listed with the first line the
# harness wrote to stderr; a run where every load fails compares nothing, and fails.
differential_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local mutants="${2:-0}" number=0 agree=0 failed=0 subject seed view differences stopped exit_shortcut exit_general
    local shortcut general errors shortcut_errors printed written
    local -a mutation arguments
    [[ "$mutants" =~ ^[0-9]+$ ]] || { usage >&2; exit 2; }
    shortcut="$(mktemp)" general="$(mktemp)" errors="$(mktemp)" shortcut_errors="$(mktemp)"
    while IFS= read -r subject; do
        for seed in $(seq 0 "$mutants"); do
            number=$((number + 1))
            mutation=()
            [[ "$seed" -eq 0 ]] || mutation=(--mutate "$seed")
            differences=""
            stopped=""
            for view in namespace initialize; do
                arguments=("--$view" "$LINUXHW/$subject.bin")
                [[ "$view" == initialize ]] && arguments+=("$PRESETS")
                exit_shortcut=0
                exit_general=0
                "$HARNESS" "${mutation[@]}" "${arguments[@]}" >"$shortcut" 2>"$shortcut_errors" || exit_shortcut=$?
                "$HARNESS" --general-load "${mutation[@]}" "${arguments[@]}" >"$general" 2>"$errors" ||
                    exit_general=$?
                sed -i -E 's/==[0-9]+==/==/g; s/0x[0-9a-f]+/0x/g' "$errors" "$shortcut_errors"
                sed -i -E -e 's/ objects=[0-9]+ objects_freed=[0-9]+$//; s/^(aml run namespace) lookups=[0-9]+ probes=[0-9]+/\1/' \
                    -e 's/^(aml run tables .*) data_built_directly=[0-9]+$/\1/' "$shortcut" "$general"
                if [[ "$exit_shortcut" -ne "$exit_general" ]]; then
                    differences+="--$view exits $exit_general without the shortcuts, $exit_shortcut with them"$'\n'
                elif [[ "$exit_general" -ne 0 && -z "$stopped" ]]; then
                    stopped="--$view exits $exit_general both ways: $(grep -m1 . "$errors" || true)"
                fi
                printed="$(diff "$general" "$shortcut" | head -5 || true)"
                written="$(diff "$errors" "$shortcut_errors" | head -5 | sed 's/^/stderr /' || true)"
                [[ -z "$printed" ]] || differences+="$printed"$'\n'
                [[ -z "$written" ]] || differences+="$written"$'\n'
            done
            if [[ -n "$differences" ]]; then
                echo "not ok $number $subject${mutation[*]:+ ${mutation[*]}}"
                sed 's/^/    # /' <<<"${differences%$'\n'}"
                continue
            fi
            agree=$((agree + 1))
            if [[ -n "$stopped" ]]; then
                failed=$((failed + 1))
                echo "# failed the same way both ways: $subject${mutation[*]:+ ${mutation[*]}}: $stopped"
            fi
        done
    done < <(reports "$1")
    rm -f "$shortcut" "$general" "$errors" "$shortcut_errors"
    echo "# differential: $agree of $number loads agree with and without the load's shortcuts, $failed of them" \
        "failing the same way both ways"
    [[ "$number" -gt 0 && "$agree" -eq "$number" && "$failed" -lt "$number" ]]
}

# Loads each machine of the list $1, and $2 mutated copies of each, with `test-acpi --read-kept --namespace`, and checks
# on each load that the check of kept data counted in data_faults exactly the kept objects the builds that end the view
# leave unbuilt (O20): as many, on the same nodes, and that the heap refused none of the builds. Prints a line per load
# that breaks it, then the totals.
kept_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local mutants="${2:-0}" number=0 agree=0 subject seed line kept=0 faults=0 unbuilt=0 refused=0
    local -a mutation fields
    [[ "$mutants" =~ ^[0-9]+$ ]] || { usage >&2; exit 2; }
    while IFS= read -r subject; do
        for seed in $(seq 0 "$mutants"); do
            number=$((number + 1))
            mutation=()
            [[ "$seed" -eq 0 ]] || mutation=(--mutate "$seed")
            line="$("$HARNESS" "${mutation[@]}" --read-kept --namespace "$LINUXHW/$subject.bin" 2>/dev/null |
                grep '^aml kept ')" || true
            read -ra fields <<<"$(sed -nE 's/^aml kept data_kept=([0-9]+) data_faults=([0-9]+) data_faults_signature=(0x[0-9A-F]+) data_faults_read=[0-9]+ read=[0-9]+ unbuilt=([0-9]+) unbuilt_signature=(0x[0-9A-F]+) refused=([0-9]+)$/\1 \2 \3 \4 \5 \6/p' <<<"$line")"
            if [[ "${#fields[@]}" -ne 6 ]]; then
                echo "not ok $number $subject${mutation[*]:+ ${mutation[*]}}: no aml kept line"
                continue
            fi
            kept=$((kept + fields[0]))
            faults=$((faults + fields[1]))
            unbuilt=$((unbuilt + fields[3]))
            refused=$((refused + fields[5]))
            if [[ "${fields[1]}" -ne "${fields[3]}" || "${fields[2]}" != "${fields[4]}" || "${fields[5]}" -ne 0 ]]; then
                echo "not ok $number $subject${mutation[*]:+ ${mutation[*]}}: data_faults=${fields[1]}" \
                    "signature ${fields[2]}, unbuilt=${fields[3]} signature ${fields[4]}, refused=${fields[5]}"
                continue
            fi
            agree=$((agree + 1))
        done
    done < <(reports "$1")
    echo "# kept data: $agree of $number loads flag the nodes their builds leave unbuilt; $kept kept, $faults" \
        "counted by the check, $unbuilt left unbuilt, $refused builds the heap refused"
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]]
}

# Prints the sum of the counter $1 over the lines $2.
total_of() {
    { grep -o " $1=[0-9]*" <<<"$2" || true; echo " $1=0"; } | cut -d= -f2 | paste -sd+ | bc
}

# Initialises each machine of the list $1 with records and without, and compares every line the two runs print but
# the counts of the records, which it sums (specification §3.6, §8.4).
predecoding_check() {
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 subject on off tables counts=""
    on="$(mktemp)" off="$(mktemp)" tables="$(mktemp -d)"
    while IFS= read -r subject; do
        number=$((number + 1))
        "$HARNESS" --loaded "$LINUXHW/$subject.bin" "$PRESETS" "$tables" >"$on" 2>&1 || true
        "$HARNESS" --predecoding off --loaded "$LINUXHW/$subject.bin" "$PRESETS" "$tables" >"$off" 2>&1 || true
        if ! grep -q '^aml run predecoding ' "$on"; then
            echo "not ok $number $subject: the harness failed"
            sed 's/^/    # /' "$on" | tail -5
        elif diff <(grep -v '^aml run predecoding ' "$on") <(grep -v '^aml run predecoding ' "$off") >/dev/null; then
            agree=$((agree + 1))
            echo "ok $number $subject: $(grep '^aml run predecoding ' "$on" | cut -d' ' -f4-)"
        else
            echo "not ok $number $subject: the runs differ"
            diff <(grep -v '^aml run predecoding ' "$off") <(grep -v '^aml run predecoding ' "$on") >"$tables/diff" || true
            head -6 "$tables/diff" | sed 's/^/    # /'
        fi
        counts+="$(grep '^aml run predecoding \|^aml run namespace ' "$on")"$'\n'
    done < <(reports "$1")
    rm -rf "$on" "$off" "$tables"
    echo -n "# predecoding: $agree of $number machines leave the same lines with records and without;"
    for key in back_edges records_run built flushes fused_comparisons tight_turns searches handed_back_loops stale_names \
        declined_shape declined_limit declined_operand declined_room declined_operation declined_full declined_target \
        declined_name; do
        echo -n " $key=$(total_of "$key" "$counts")"
    done
    echo
    [[ "$number" -gt 0 && "$agree" -eq "$number" && "$(total_of stale_names "$counts")" -eq 0 ]]
}

# Prints the statements the records of a run ran or handed back, from its `aml run predecoding` line in file $1.
records_asked() {
    grep -m1 '^aml run predecoding ' "$1" | grep -oE ' (records_run|declined_[a-z]+)=[0-9]+' | cut -d= -f2 |
        awk '{ sum += $1 } END { print sum + 0 }'
}

# Prints the instructions callgrind counted in its output file $1, or nothing when it wrote none.
callgrind_total() {
    [[ -s "$1" ]] && grep -m1 '^summary:' "$1" | cut -d' ' -f2
}

# Prints the last lines of each file named that exists, as remarks.
records_cost_tail() {
    local file
    for file in "$@"; do
        [[ -s "$file" ]] && tail -n 3 "$file" | sed "s|^|    # $(basename "$file"): |"
    done
    return 0
}

# Prints the percentage by which $1 passes $2, to four decimals.
records_cost_percent() {
    awk -v on="$1" -v off="$2" 'BEGIN { printf "%+.4f%%", 100 * (on - off) / off }'
}

# Counts, under callgrind, the instructions `test-acpi-cost --initialize` executes on each machine of the list $1 whose
# loops ask the records anything, with records and without, and fails when a machine costs more with them than
# RECORDS_TOLERANCE percent above without, when its two runs differ, when the harness or callgrind fails on it, or
# when no machine is compared (specification §3.6, §8.4).
records_cost_check() {
    local valgrind="${VALGRIND:-valgrind}" tolerance="${RECORDS_TOLERANCE:-0.5}" harness="$ROOT/build/acpi-cost/test-acpi-cost"
    local number=0 line=0 printed=0 measured=0 above=0 differ=0 failed=0 total_on=0 total_off=0
    local subject on off on_status off_status verdict
    if [[ ! "$tolerance" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
        echo "records-cost: RECORDS_TOLERANCE=$tolerance is no decimal number of percent" >&2
        return 2
    fi
    if ! command -v "$valgrind" >/dev/null 2>&1; then
        echo "records-cost: no valgrind ($valgrind): install it, or name it in VALGRIND" >&2
        return 1
    fi
    if ! (cd "$ROOT" && xmake build -q test-acpi-cost) || [[ ! -x "$harness" ]]; then
        echo "records-cost: xmake could not build test-acpi-cost: run xmake build test-acpi-cost to see why" >&2
        return 1
    fi
    local work
    work="$(mktemp -d)"
    EXIT_CLEANUP+=("$work")
    while IFS= read -r subject; do
        number=$((number + 1))
        "$harness" --initialize "$LINUXHW/$subject.bin" "$PRESETS" >"$work/probe" 2>&1 || true
        if ! grep -q '^aml run predecoding ' "$work/probe"; then
            line=$((line + 1))
            echo "not ok $line $subject: the harness printed no count of the records"
            failed=$((failed + 1))
            continue
        fi
        printed=$((printed + 1))
        [[ "$(records_asked "$work/probe")" -gt 0 ]] || continue
        measured=$((measured + 1))
        line=$((line + 1))
        rm -f "$work/on.callgrind" "$work/off.callgrind" "$work/on.log" "$work/off.log"
        on_status=0
        off_status=0
        "$valgrind" --tool=callgrind --log-file="$work/on.log" --callgrind-out-file="$work/on.callgrind" \
            "$harness" --initialize "$LINUXHW/$subject.bin" "$PRESETS" >"$work/on" 2>&1 || on_status=$?
        "$valgrind" --tool=callgrind --log-file="$work/off.log" --callgrind-out-file="$work/off.callgrind" \
            "$harness" --predecoding off --initialize "$LINUXHW/$subject.bin" "$PRESETS" >"$work/off" 2>&1 ||
            off_status=$?
        on="$(callgrind_total "$work/on.callgrind" || true)"
        off="$(callgrind_total "$work/off.callgrind" || true)"
        if [[ -z "$on" || -z "$off" ]]; then
            echo "not ok $line $subject: callgrind counted nothing"
            records_cost_tail "$work/on.log" "$work/off.log" "$work/on"
            failed=$((failed + 1))
            continue
        fi
        if [[ "$on_status" -ne 0 || "$off_status" -ne 0 ]]; then
            echo "not ok $line $subject: the harness exited $on_status with records and $off_status without, under callgrind"
            records_cost_tail "$work/on" "$work/off"
            failed=$((failed + 1))
            continue
        fi
        if ! diff -q <(grep -v '^aml run predecoding ' "$work/on") <(grep -v '^aml run predecoding ' "$work/off") \
            >/dev/null; then
            echo "not ok $line $subject: the runs with records and without print different lines"
            diff <(grep -v '^aml run predecoding ' "$work/off") <(grep -v '^aml run predecoding ' "$work/on") |
                head -5 | sed 's/^/    # /' || true
            differ=$((differ + 1))
            continue
        fi
        total_on=$((total_on + on))
        total_off=$((total_off + off))
        verdict="ok"
        if awk -v on="$on" -v off="$off" -v tolerance="$tolerance" 'BEGIN { exit !(100 * (on - off) > tolerance * off) }'; then
            verdict="not ok"
            above=$((above + 1))
        fi
        echo "$verdict $line $subject: on=$on off=$off on-off=$((on - off)) ($(records_cost_percent "$on" "$off"))"
    done < <(reports "$1")
    if [[ "$printed" -eq 0 ]]; then
        echo "records-cost: the harness printed no count of the records on any of the $number machines of the list:" \
            "nothing compared" >&2
        return 1
    fi
    if [[ "$measured" -eq 0 ]]; then
        echo "records-cost: no machine of the $number of the list asks the records anything: nothing compared" >&2
        return 1
    fi
    local share=""
    [[ "$total_off" -gt 0 ]] && share=" ($(records_cost_percent "$total_on" "$total_off"))"
    echo "# records-cost: $measured of $number machines ask the records; on=$total_on off=$total_off$share;" \
        "above +$tolerance%: $above; runs that differ: $differ; failed: $failed"
    [[ "$above" -eq 0 && "$differ" -eq 0 && "$failed" -eq 0 ]]
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

# Prints, one per line, the section, a space and the machine of each line of tools/acpi/slack-decided.list but its
# comments.
slack_decided() {
    grep -v '^#' "$ROOT/tools/acpi/slack-decided.list" | grep -v '^$' || true
}

# Evaluates the needs path of each machine of the list $1 with acpiexec, then with `acpiexec -es`, through
# tools/acpi/acpiexec_slack.py, on the tables `test-acpi --loaded` ends with, and lists the machines whose results
# differ: those that rely on slack behaviour (specification §8.6). One that tools/acpi/slack-decided.list names is
# decided; one it does not name fails the run, and so does one it names that does not differ, and a machine the oracle
# gives no verdict on.
slack_check() {
    [[ -x "$ACPICA_BIN/acpiexec" ]] || { echo "slack: no acpiexec, run fetch acpica first" >&2; exit 1; }
    (cd "$ROOT" && xmake build -q test-acpi)
    local number=0 agree=0 stopped=0 failed=0 decided=0 undecided=0 stale=0 evaluations=0 status_only=0 subject verdict
    local madt tables section machine relying=""
    local -a written
    local -A sections=()
    while read -r section machine; do
        sections["$machine"]="$section"
    done < <(slack_decided)
    while IFS= read -r subject; do
        number=$((number + 1))
        if ! has_decoded "$subject"; then
            echo "not ok $number $subject: no decoded report, run fetch first"
            failed=$((failed + 1))
            continue
        fi
        madt="$(mktemp -p "$HARNESS_WORK")" tables="$(mktemp -d -p "$HARNESS_WORK")"
        "$HARNESS" --loaded "$LINUXHW/$subject.bin" "$PRESETS" "$tables" >/dev/null 2>&1 || true
        mapfile -t written < <(find "$tables" -name '*.dat' | sort)
        if [[ "${#written[@]}" -eq 0 ]]; then
            echo "not ok $number $subject: the harness wrote no table"
            failed=$((failed + 1))
            continue
        fi
        decoded "$subject" | python3 -I "$ROOT/tools/acpi/expected_madt.py" "$LINUXHW/$subject.bin" - >"$madt" || true
        verdict="$(python3 -I "$ROOT/tools/acpi/acpiexec_slack.py" "$ACPICA_BIN" "$madt" "$PRESETS" "${written[@]}" \
            2>&1)" || true
        section="${sections["$subject"]:-}"
        if grep -q '^aml slack differ ' <<<"$verdict"; then
            if [[ -n "$section" ]]; then
                echo "ok $number $subject: relies on slack behaviour, as $section decides"
                decided=$((decided + 1))
            else
                echo "not ok $number $subject: relies on slack behaviour, undecided by tools/acpi/slack-decided.list"
                relying+="$subject"$'\n'
                undecided=$((undecided + 1))
            fi
            grep '^aml slack differ ' <<<"$verdict" | head -8 | cut -c1-200 | sed 's/^/    # /'
        elif [[ -n "$section" ]] && grep -q '^aml slack \(agree\|unevaluated\) ' <<<"$verdict"; then
            echo "not ok $number $subject: tools/acpi/slack-decided.list names it, and it does not differ:" \
                "$(cut -d' ' -f3- <<<"$verdict")"
            stale=$((stale + 1))
        elif grep -q '^aml slack agree ' <<<"$verdict"; then
            echo "ok $number $subject: $(cut -d' ' -f4- <<<"$verdict")"
            agree=$((agree + 1))
            evaluations=$((evaluations + $(grep -o ' evaluations=[0-9]*' <<<"$verdict" | cut -d= -f2)))
            status_only=$((status_only + $(grep -o ' status_only=[0-9]*' <<<"$verdict" | cut -d= -f2)))
        elif grep -q '^aml slack unevaluated ' <<<"$verdict"; then
            echo "ok $number $subject # SKIP acpiexec stops before $(cut -d' ' -f4 <<<"$verdict") in both runs"
            stopped=$((stopped + 1))
        else
            echo "not ok $number $subject: the oracle gave no verdict"
            tail -5 <<<"$verdict" | sed 's/^/    # /'
            failed=$((failed + 1))
        fi
    done < <(reports "$1")
    echo "# slack: $((decided + undecided)) of $((number - stopped - failed - stale)) machines rely on" \
        "slack behaviour, $decided as the specification decides; $agree agree over $evaluations evaluations," \
        "$status_only of them on status only; acpiexec stops before the same evaluation in both runs on $stopped;" \
        "listed but not differing: $stale; no verdict on $failed"
    [[ -z "$relying" ]] || sed 's/^/# relies on slack behaviour, undecided: /' <<<"${relying%$'\n'}"
    [[ "$number" -gt 0 && -z "$relying" && "$stale" -eq 0 && "$failed" -eq 0 ]]
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
# comparison held; fails on kept data the check of kept data finds would not build.
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
    echo "# kept data: $(total_of data_faults "$faults") objects that would not build, on" \
        "$(grep -c ' data_faults=[1-9]' <<<"$faults") machines; $(total_of data_named_sizes "$faults") whose size a" \
        "name gives, read at their first read"
    [[ "$(grep -c ' iasl \(agrees\|differs\|missing\) ' <<<"$disassembly")" -eq "$tables" ]] ||
        { echo "# iASL comparison: not every table was compared" >&2; return 1; }
    [[ "$(total_of data_faults "$faults")" -eq 0 ]] ||
        { echo "# kept data: an object that would not build waits for its first read" >&2; return 1; }
    [[ "$number" -gt 0 && "$agree" -eq "$number" ]] &&
        [[ "$oracle" -eq 2 || "$(grep -c '^fadt acpica agrees' <<<"$acpica")" -eq "$number" ]]
}

# Prints, for each opcode the needs-path closure of some machine of the list $1, or of some QEMU configuration,
# reaches, the number of machines it reaches it on, and their names when they are five or fewer, then each one the run
# list leaves out where it is reached, with every machine it is reached on; fails on such an opcode (specification
# §4.1), when a machine could not be read, or when decoding met a fault, which cuts a body and the closure with it.
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
            /^aml closure unexecuted / {
                if (!($4 in unexecuted)) ++unexecuted_count
                unexecuted_machines[$4] = unexecuted_machines[$4] ((unexecuted[$4]++ == 0) ? "" : "; ") current
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
                printf "# closure: %u opcodes reached on some machine of %u, %u of them not executed where they are reached;" \
                    " %u observations dropped; %u decode faults\n", count, subjects, unexecuted_count, dropped, faults
                for (opcode in unexecuted)
                    printf "# not executed: %s, reached on %u: %s\n", opcode, unexecuted[opcode],
                        unexecuted_machines[opcode]
                for (index_ = 1; index_ <= failures; ++index_) printf "# not read: %s\n", unread[index_]
                exit (seen != subjects || faults != 0 || unexecuted_count != 0) ? 1 : 0
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

# Boots the kernel $2 on the QEMU machine $1 as tools/instructions.sh boots it, until it has printed its tables,
# and writes them as that machine's module, whose path it prints: the tables of a release kernel's windows.
qemu_module() {
    [[ "$#" -eq 2 && ( "$1" == q35 || "$1" == pc ) && -f "$2" ]] || { usage >&2; exit 2; }
    (cd "$ROOT" && xmake build -q test-acpi)
    mkdir -p "$MODULES"
    local printed module="$MODULES/$1.acpi"
    printed="$(mktemp -d)"
    if ! "$ROOT/tools/instructions.sh" --top 0 --until '[acpidump] end' --machine "$1" \
        --append lpl.acpi.tables=print --keep "$printed" "$2" >/dev/null; then
        rm -rf "$printed"
        echo "module: $2 printed no tables on $1" >&2
        exit 1
    fi
    "$HARNESS" --write-printed-module "$printed/serial.log" "$module" || { rm -rf "$printed"; exit 1; }
    rm -rf "$printed"
    echo "$module"
}

# Boots the kernel $1 on the QEMU machine $2, its serial port written to the log $3, with the boot line $4
# and the module $5 when there is one, until its tests print their totals or BOOT_SECONDS pass. Both are
# copied under a short path first: QEMU puts the kernel's path at the head of the boot line, of which the
# kernel keeps 1,023 characters (BOOT_COMMAND_LINE_CAPACITY) and fails a test run it cut, and takes a space
# or a comma in a module's path for a separator.
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
# $4, with what differs. tools/parity.sh compares only the tests the host ran, so the tests $5, which read the
# subject's tables, must have run on the host; ring 0 is held to them by lpl.test.require.
compare_ring0() {
    local result test
    if ! grep -qaE '^# Totals: ' "$2"; then
        echo "not ok $4 $3: the boot printed no '# Totals:': it hung or crashed in its tests"
        return 1
    fi
    if ! grep -qaE '^\[LPLTLM\] telemetry .* duplicate_keys=0 sanitised_characters=0 dropped_fields=0$' "$2"; then
        echo "not ok $4 $3: the boot's telemetry is missing, or kept a duplicate key, a character it had to sanitise or a"
        echo "    # field past a record's cap: $(grep -aE '^\[LPLTLM\] telemetry ' "$2" || echo 'no telemetry record')"
        return 1
    fi
    for test in $5; do
        if ! grep -qE "^    ok [0-9]+ $test\$" "$1"; then
            echo "not ok $4 $3: $test did not run on the host"
            grep -E "^test-acpi: | $test( |\$)" "$1" | sed 's/^/    # /'
            return 1
        fi
    done
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
    local number=0 agree=0 subject logs tests
    while IFS= read -r subject; do
        number=$((number + 1))
        logs="$RING0_LOGS/$(module_name "$subject")"
        mkdir -p "$logs"
        if [[ "$1" == qemu ]]; then
            tests=the_booted_machine_loads
            boot "$2" "$subject" "$logs/serial.log" \
                "lpl.acpi.tables=print lpl.test=$SUITES* lpl.test.require=${SUITES}_corpus.$tests"
            "$HARNESS" --firmware-log "$logs/serial.log" >"$logs/host.log" 2>&1 || true
        else
            tests="the_boot_module_machine_loads the_boot_module_machine_loads_on_the_release_path"
            if ! "$HARNESS" --write-module "$LINUXHW/$subject.bin" "$logs/machine.acpi" 2>"$logs/host.log"; then
                echo "not ok $number $subject: $(cat "$logs/host.log")"
                continue
            fi
            "$HARNESS" --boot-module "$logs/machine.acpi" >"$logs/host.log" 2>&1 || true
            boot "$2" q35 "$logs/serial.log" \
                "lpl.test=$SUITES* lpl.test.require=${SUITES}_corpus.the_boot_module_machine_loads*" "$logs/machine.acpi"
        fi
        compare_ring0 "$logs/host.log" "$logs/serial.log" "$subject" "$number" "$tests" && agree=$((agree + 1))
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
    slack) known_list "${2:-corpus}"; slack_check "${2:-corpus}" ;;
    load) known_list "${2:-corpus}"; load_check "${2:-corpus}" ;;
    differential) known_list "${2:-corpus}"; differential_check "${2:-corpus}" "${3:-0}" ;;
    kept) known_list "${2:-corpus}"; kept_check "${2:-corpus}" "${3:-0}" ;;
    predecoding) known_list "${2:-corpus}"; predecoding_check "${2:-corpus}" ;;
    records-cost)
        case "${2:-corpus}" in
            corpus | full) records_cost_check "${2:-corpus}" ;;
            *) usage >&2; exit 2 ;;
        esac ;;
    check) known_list "${2:-corpus}"; check "${2:-corpus}" ;;
    uacpi) uacpi_check ;;
    module)
        shift
        if [[ "${1:-}" == qemu ]]; then shift; qemu_module "$@"; else modules "$@"; fi
        ;;
    ring0)
        [[ "$#" -eq 3 ]] || { usage >&2; exit 2; }
        [[ "$2" == qemu ]] || known_list "$2"
        ring0 "$2" "$3"
        ;;
    -h | --help) usage ;;
    *) usage >&2; exit 2 ;;
esac
