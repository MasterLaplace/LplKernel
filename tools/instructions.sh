#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/instructions.sh [options] <kernel>

Counts what <kernel> executes under QEMU's TCG: instructions, memory loads and memory stores,
in total and per function. The run uses -icount, so virtual time follows the instruction count
and two runs of the same image count the same numbers, on any machine, unlike a clock.

  --test NAME     count one test declared with LPL_TEST: from its body to the line that reports it
  --from SYMBOL   start counting the first time SYMBOL runs (default: from the first instruction)
  --to SYMBOL     stop counting the first time SYMBOL runs after that; needs --from
  --times N       stop on the Nth run of --to's SYMBOL instead (default: 1); with --from and --to
                  the same function, the window holds exactly N of its runs
  --inside SYMBOL count only from each entry of SYMBOL to its return, over its first --times calls;
                  what runs between two calls, such as a loop waiting for the next one, is left out
  --skip N        with --inside, leave its first N calls out and count the --times calls after them:
                  a test that runs a function the boot ran before it, on other data, is its second call
  --until TEXT    without --from or --inside, stop when the serial output prints TEXT
                  (default: "# Totals:")
  --top N         functions listed, most instructions first (default: 25; 0 lists none)
  --append TEXT   kernel command line
  --module FILE   give FILE to the kernel as a Multiboot module (QEMU's -initrd), under a short path
                  that keeps its file name: a recorded machine of the ACPI corpus, which
                  tools/acpi-corpus.sh module <machine> writes, is found by its .acpi suffix
  --machine NAME  QEMU's machine, such as q35 (default: QEMU's own, pc)
  --cdrom ISO     boot ISO through GRUB instead of <kernel> alone: a client gets its display and its
                  cartridge only that way. <kernel> is still the image the ISO holds, which names
                  the functions; GRUB passes no --append
  --timeout S     give up after S seconds (default: 1800)
  --keep DIR      keep the serial log and the raw counts in DIR
  --budget FILE   check the counts against FILE, which records a window and the counts it held

A budget FILE holds key=value lines: from, to, inside, times, skip, machine, append and module give
the window, unless the command line does, append being the boot line, such as the lpl.test= that picks
the tests run before a window inside one, and module a path from the repository's root;
instructions and accesses (loads and stores) record the counts;
tolerance is how far above them, in percent, the counts may go. Below them by more than the
tolerance, the run says to record the new counts.

A SYMBOL is a function's name as the symbol table spells it, or its demangled name up to the
parenthesis of its parameters. One processor only: QEMU 10.2 crashes when -icount, a plugin and
-smp above 1 meet.

The QEMU plugin is built on first use, under build/qemu-plugin/, against the plugin header of
the QEMU it finds (qemu-system-i386 --version), fetched from QEMU's repository at that tag.

Exit status: 0 when the counts were written, 1 when QEMU stopped or timed out before the end
of the window, 2 on a usage error, 3 when a count passed its budget.
EOF
}

from_symbol=""
to_symbol=""
inside_symbol=""
until_text="# Totals:"
top=25
append=""
machine=""
timeout_seconds=1800
times=""
skip=""
module=""
keep=""
budget=""
kernel=""
cdrom=""

while [ $# -gt 0 ]; do
    case "$1" in
        --from) from_symbol="${2:?}"; shift 2 ;;
        --to) to_symbol="${2:?}"; shift 2 ;;
        --times) times="${2:?}"; shift 2 ;;
        --inside) inside_symbol="${2:?}"; shift 2 ;;
        --skip) skip="${2:?}"; shift 2 ;;
        --until) until_text="${2:?}"; shift 2 ;;
        --top) top="${2:?}"; shift 2 ;;
        --test) from_symbol="lplTestBody_${2:?}"; to_symbol="lpl::testing::(anonymous namespace)::writeResult"; shift 2 ;;
        --append) append="${2:?}"; shift 2 ;;
        --module) module="${2:?}"; shift 2 ;;
        --machine) machine="${2:?}"; shift 2 ;;
        --cdrom) cdrom="${2:?}"; shift 2 ;;
        --timeout) timeout_seconds="${2:?}"; shift 2 ;;
        --keep) keep="${2:?}"; shift 2 ;;
        --budget) budget="${2:?}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "unknown option $1" >&2; usage >&2; exit 2 ;;
        *) [ -z "$kernel" ] || { usage >&2; exit 2; }; kernel="$1"; shift ;;
    esac
done
[ -n "$kernel" ] && [ -f "$kernel" ] || { usage >&2; exit 2; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -z "$cdrom" ] || [ -f "$cdrom" ] || { echo "no ISO $cdrom" >&2; exit 2; }
recorded_instructions=""
recorded_accesses=""
tolerance=0
if [ -n "$budget" ]; then
    [ -f "$budget" ] || { echo "no budget file $budget" >&2; exit 2; }
    while IFS='=' read -r key value; do
        case "$key" in
            ""|"#"*) ;;
            from) [ -n "$from_symbol" ] || from_symbol="$value" ;;
            to) [ -n "$to_symbol" ] || to_symbol="$value" ;;
            inside) [ -n "$inside_symbol" ] || inside_symbol="$value" ;;
            times) [ -n "$times" ] || times="$value" ;;
            skip) [ -n "$skip" ] || skip="$value" ;;
            module) [ -n "$module" ] || module="$root/$value" ;;
            machine) [ -n "$machine" ] || machine="$value" ;;
            append) [ -n "$append" ] || append="$value" ;;
            instructions) recorded_instructions="$value" ;;
            accesses) recorded_accesses="$value" ;;
            tolerance) tolerance="$value" ;;
            *) echo "unknown key $key in $budget" >&2; exit 2 ;;
        esac
    done <"$budget"
    [[ "$recorded_instructions" =~ ^[1-9][0-9]*$ && "$recorded_accesses" =~ ^[1-9][0-9]*$ && "$tolerance" =~ ^[0-9]+$ ]] ||
        { echo "$budget needs instructions and accesses above 0, and tolerance, as integers" >&2; exit 2; }
fi
times="${times:-1}"
skip="${skip:-0}"
[[ "$skip" =~ ^[0-9]+$ ]] || { echo "--skip takes a count of calls" >&2; exit 2; }
[ "$skip" -eq 0 ] || [ -n "$inside_symbol" ] || { echo "--skip goes with --inside" >&2; exit 2; }
[ -z "$module" ] || [ -f "$module" ] ||
    { echo "no module $module: tools/acpi-corpus.sh module <machine> writes a recorded machine's module" >&2; exit 2; }
[ -z "$module" ] || [ -z "$cdrom" ] || { echo "--module reaches a kernel booted alone, not one GRUB boots" >&2; exit 2; }
[ -z "$cdrom" ] || [ -z "$append" ] || { echo "--append reaches a kernel booted alone, not one GRUB boots" >&2; exit 2; }
if [ -n "$from_symbol" ] && [ -z "$to_symbol" ] || [ -z "$from_symbol" ] && [ -n "$to_symbol" ]; then
    echo "--from and --to go together" >&2
    exit 2
fi
if [ -n "$inside_symbol" ] && [ -n "$from_symbol" ]; then
    echo "--inside goes without --from, --to and --test" >&2
    exit 2
fi

qemu_version="$(qemu-system-i386 --version | sed -n 's/^QEMU emulator version \([0-9.]*\).*/\1/p')"
[ -n "$qemu_version" ] || { echo "qemu-system-i386 not found" >&2; exit 2; }
plugin_dir="$root/build/qemu-plugin/$qemu_version"
plugin="$plugin_dir/libinstruction_count.so"

if [ ! -f "$plugin" ] || [ "$root/tools/qemu/instruction_count.c" -nt "$plugin" ]; then
    mkdir -p "$plugin_dir"
    if [ ! -f "$plugin_dir/qemu-plugin.h" ]; then
        curl -sfL -o "$plugin_dir/qemu-plugin.h.part" \
            "https://gitlab.com/qemu-project/qemu/-/raw/v$qemu_version/include/qemu/qemu-plugin.h"
        mv "$plugin_dir/qemu-plugin.h.part" "$plugin_dir/qemu-plugin.h"
    fi
    # shellcheck disable=SC2046
    cc -O2 -Wall -Wextra -Werror -shared -fPIC -I"$plugin_dir" $(pkg-config --cflags glib-2.0) \
        -o "$plugin" "$root/tools/qemu/instruction_count.c" $(pkg-config --libs glib-2.0)
fi

nm_tool="i686-elf-nm"
command -v "$nm_tool" >/dev/null 2>&1 || nm_tool="nm"
address_of() {
    "$nm_tool" -C "$kernel" | awk -v name="$1" '
        $2 ~ /^[TtWw]$/ {
            address = $1; $1 = ""; $2 = ""; sub(/^ +/, "")
            if ($0 == name || index($0, name "(") == 1) { found[++count] = address }
        }
        END { if (count == 1) print "0x" found[1]; else if (count > 1) print "ambiguous" }'
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
counts="$work/counts"
serial="$work/serial.log"
plugin_args="out=$counts"
if [ -n "$from_symbol" ]; then
    begin="$(address_of "$from_symbol")"
    end="$(address_of "$to_symbol")"
    for found in "$from_symbol=$begin" "$to_symbol=$end"; do
        case "${found##*=}" in
            "") echo "no function ${found%=*} in $kernel" >&2; exit 2 ;;
            ambiguous) echo "more than one function ${found%=*} in $kernel" >&2; exit 2 ;;
        esac
    done
    plugin_args="$plugin_args,begin=$begin,end=$end,times=$times"
fi
if [ -n "$inside_symbol" ]; then
    inside="$(address_of "$inside_symbol")"
    case "$inside" in
        "") echo "no function $inside_symbol in $kernel" >&2; exit 2 ;;
        ambiguous) echo "more than one function $inside_symbol in $kernel" >&2; exit 2 ;;
    esac
    plugin_args="$plugin_args,inside=$inside,times=$times,skip=$skip"
fi

boot=(-kernel "$kernel")
[ -z "$cdrom" ] || boot=(-cdrom "$cdrom")
if [ -n "$module" ]; then
    module_copy="$work/$(basename "$module" | tr -c 'A-Za-z0-9.\n-' '_')"
    cp "$module" "$module_copy"
    boot+=(-initrd "$module_copy")
fi
qemu_command=(qemu-system-i386 "${boot[@]}" -smp 1 -m 512 -display none -no-reboot -monitor none
              -serial "file:$serial" -icount shift=0,sleep=off -plugin "$plugin,$plugin_args")
[ -z "$append" ] || qemu_command+=(-append "$append")
[ -z "$machine" ] || qemu_command+=(-machine "$machine")
"${qemu_command[@]}" &
qemu_pid=$!

finished=0
for _ in $(seq 1 "$timeout_seconds"); do
    if [ -n "$from_symbol$inside_symbol" ] && [ -s "$counts" ]; then
        finished=1
        break
    fi
    if [ -z "$from_symbol$inside_symbol" ] && [ -f "$serial" ] && grep -aqF "$until_text" "$serial"; then
        finished=1
        break
    fi
    kill -0 "$qemu_pid" 2>/dev/null || break
    sleep 1
done
kill -TERM "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true

if [ -n "$keep" ]; then
    mkdir -p "$keep"
    cp -f "$serial" "$keep/" 2>/dev/null || true
    cp -f "$counts" "$keep/" 2>/dev/null || true
fi
if [ "$finished" -ne 1 ] || [ ! -s "$counts" ]; then
    echo "the window did not close: QEMU stopped or the run timed out before it" >&2
    exit 1
fi

"$nm_tool" -n -C "$kernel" | awk '$2 ~ /^[TtWw]$/ { address = $1; $1 = ""; $2 = ""; sub(/^ +/, ""); print address "\t" $0 }' \
    >"$work/symbols"
awk -v top="$top" '
    function hex(text) { return strtonum("0x" text) }
    FNR == NR {
        split($0, field, "\t")
        starts[++symbols] = hex(field[1])
        names[symbols] = field[2]
        next
    }
    /^#/ { next }
    {
        address = $1; instructions = $2 * $3
        low = 1; high = symbols; found = 0
        while (low <= high) {
            middle = int((low + high) / 2)
            if (starts[middle] <= address) { found = middle; low = middle + 1 } else high = middle - 1
        }
        name = found ? names[found] : "(no symbol)"
        cost[name] += instructions; loads[name] += $4; stores[name] += $5
        total += instructions; total_loads += $4; total_stores += $5
    }
    END {
        printf "instructions=%d loads=%d stores=%d\n", total, total_loads, total_stores
        if (top == 0 || total == 0) exit
        printf "%7s %14s %14s %14s  %s\n", "share", "instructions", "loads", "stores", "function"
        listed = 0
        while (listed < top) {
            best = ""; best_cost = -1
            for (name in cost) if (cost[name] > best_cost) { best = name; best_cost = cost[name] }
            if (best == "") break
            printf "%6.2f%% %14d %14d %14d  %s\n", 100 * best_cost / total, best_cost, loads[best], stores[best], best
            delete cost[best]
            ++listed
        }
    }
' "$work/symbols" "$counts" >"$work/report"
cat "$work/report"
[ -n "$budget" ] || exit 0

read -r counted_instructions counted_loads counted_stores < <(
    sed -n '1s/^instructions=\([0-9]*\) loads=\([0-9]*\) stores=\([0-9]*\)$/\1 \2 \3/p' "$work/report")
within=1
check_budget() {
    local name="$1" counted="$2" recorded="$3"
    awk -v name="$name" -v counted="$counted" -v recorded="$recorded" -v tolerance="$tolerance" \
        'BEGIN { printf "budget %s: %d counted, %d recorded (%+.2f %%, tolerance %d %%)\n",
                 name, counted, recorded, 100 * (counted - recorded) / recorded, tolerance }'
    if [ $((counted * 100)) -gt $((recorded * (100 + tolerance))) ]; then
        echo "over budget: $name $counted, above $recorded by more than $tolerance %" >&2
        within=0
    elif [ $((counted * 100)) -lt $((recorded * (100 - tolerance))) ]; then
        echo "$name $counted is below $recorded by more than $tolerance %: record it in $budget"
    fi
}
check_budget instructions "$counted_instructions" "$recorded_instructions"
check_budget accesses "$((counted_loads + counted_stores))" "$recorded_accesses"
[ "$within" -eq 1 ] || exit 3
