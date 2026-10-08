#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/parity.sh <host-log>... <serial-log>
       tools/parity.sh --self-test

Compares the records of the tests declared with LPL_TEST between the host and ring 0. A
record is a line `# <suite>.<test>: <key>=<value>` that a test measures; it knows no field,
so a new value in a test is compared without a change here.

Each <host-log> is what a host test binary printed: LplPlugin's `test-engine`, LplAssistant's
`test-assistant`, LplKnowledge's `test-knowledge`. <serial-log> is what a debug kernel printed
on COM1. Every test the hosts ran must have run in ring 0 and printed the same records, in the
same order. Tests that exist on one side only (the kernel's own, or the tests of what only the
kernel compiles) are not compared.

  --self-test  check that a difference, a missing test, a repeated run and an empty host log
               all fail, and that several host logs and a serial log with carriage returns
               still compare

Exit status: 0 when every test the host ran agrees, 1 when one does not or when the host
log holds no test, 2 on a usage error.
EOF
}

# Reads KTAP logs and prints one line per test: `<suite>.<test> <status> <records>`, where
# <status> is ok, failed, skipped, or repeated for a test the logs report more than once (a
# reboot appended to the same log, or two repositories declaring one name), and <records> joins
# the test's `key=value` with `|`.
summarize() {
    awk '
        { sub(/\r$/, "") }
        /^    # Subtest: / { suite = $3; next }
        /^    # [A-Za-z0-9_]+\.[A-Za-z0-9_]+: [A-Za-z0-9_]+=[^ ]+$/ {
            name = substr($2, 1, length($2) - 1)
            pending[name] = pending[name] (pending[name] == "" ? "" : "|") $3
            next
        }
        /^    (not )?ok [0-9]+ [A-Za-z0-9_]+/ {
            name = suite "." (($1 == "not") ? $4 : $3)
            status[name] = ($0 ~ / # SKIP /) ? "skipped" : (($1 == "not") ? "failed" : "ok")
            records[name] = (name in pending) ? pending[name] : "-"
            delete pending[name]
            if (++runs[name] > 1)
                status[name] = "repeated"
        }
        END {
            for (name in status)
                print name, status[name], records[name]
        }
    ' "$@"
}

# Compares the host logs, every argument but the last, with the serial log, the last.
compare() {
    local ring0="${*: -1}"
    local hosts=("${@:1:$#-1}")

    LC_ALL=C join -a 1 -e missing -o 0,1.2,1.3,2.2,2.3 \
        <(summarize "${hosts[@]}" | LC_ALL=C sort -k1,1) <(summarize "$ring0" | LC_ALL=C sort -k1,1) |
        awk '
            $2 == "skipped" { next }
            {
                ++compared
                if ($2 == "failed") { print "not ok " $1 ": fails on the host"; ++differences; next }
                if ($2 == "repeated") { print "not ok " $1 ": ran more than once on the host"; ++differences; next }
                if ($4 == "missing") { print "not ok " $1 ": did not run in ring 0"; ++differences; next }
                if ($4 == "repeated") { print "not ok " $1 ": ran more than once in ring 0"; ++differences; next }
                if ($4 != "ok") { print "not ok " $1 ": " $4 " in ring 0"; ++differences; next }
                host = split(($3 == "-") ? "" : $3, hostRecords, "|")
                ring0 = split(($5 == "-") ? "" : $5, ring0Records, "|")
                records += host
                same = (host == ring0)
                for (i = 1; same && i <= host; ++i)
                    same = (hostRecords[i] == ring0Records[i])
                if (same) { print "ok " $1 ": " host " records agree"; next }
                ++differences
                for (i = 1; i <= host || i <= ring0; ++i)
                    if (hostRecords[i] != ring0Records[i])
                        printf "not ok %s: %s on the host, %s in ring 0\n", $1,
                               (i <= host) ? hostRecords[i] : "nothing", (i <= ring0) ? ring0Records[i] : "nothing"
            }
            END {
                printf "# %d tests compared, %d records, %d differences\n", compared, records, differences
                exit (compared == 0 || differences != 0) ? 1 : 0
            }
        '
}

self_test() {
    local directory
    directory="$(mktemp -d)"
    trap 'rm -rf "$directory"' RETURN

    cat >"$directory/host" <<'EOF'
KTAP version 1
1..1
    KTAP version 1
    # Subtest: gate
    1..3
    # gate.fold: signature=0x00000001
    # gate.fold: cells=3
    ok 1 fold
    ok 2 quiet
    ok 3 other # SKIP not selected
ok 1 gate
EOF
    sed 's/cells=3/cells=4/' "$directory/host" >"$directory/different"
    grep -v '^    ok 1 fold' "$directory/host" >"$directory/missing"
    sed 's/^    ok 2 quiet/    # gate.quiet: signature=0x00000001\n&/' "$directory/host" >"$directory/extra"
    sed 's/$/\r/' "$directory/host" >"$directory/carriage"
    cat "$directory/host" "$directory/host" >"$directory/twice"
    printf 'KTAP version 1\n1..0\n' >"$directory/empty"
    cat >"$directory/second" <<'EOF'
KTAP version 1
1..1
    KTAP version 1
    # Subtest: other
    1..1
    # other.fold: signature=0x00000002
    ok 1 fold
ok 1 other
EOF
    cat "$directory/host" "$directory/second" >"$directory/both"

    local failures=0
    compare "$directory/host" "$directory/host" >/dev/null || { echo "self-test: a log differs from itself"; failures=$((failures + 1)); }
    ! compare "$directory/host" "$directory/different" >/dev/null || { echo "self-test: a different record passed"; failures=$((failures + 1)); }
    ! compare "$directory/host" "$directory/missing" >/dev/null || { echo "self-test: a missing test passed"; failures=$((failures + 1)); }
    ! compare "$directory/empty" "$directory/host" >/dev/null || { echo "self-test: an empty host log passed"; failures=$((failures + 1)); }
    { compare "$directory/host" "$directory/extra" || true; } | grep -q '^not ok gate.quiet:' || { echo "self-test: a record only ring 0 printed was not named"; failures=$((failures + 1)); }
    compare "$directory/host" "$directory/carriage" >/dev/null || { echo "self-test: carriage returns broke the comparison"; failures=$((failures + 1)); }
    ! compare "$directory/host" "$directory/twice" >/dev/null || { echo "self-test: a log holding the run twice passed"; failures=$((failures + 1)); }
    compare "$directory/host" "$directory/second" "$directory/both" >/dev/null || { echo "self-test: two host logs did not compare with one serial log"; failures=$((failures + 1)); }
    ! compare "$directory/host" "$directory/second" "$directory/host" >/dev/null || { echo "self-test: a test of the second host log that ring 0 did not run passed"; failures=$((failures + 1)); }
    ! compare "$directory/host" "$directory/host" "$directory/host" >/dev/null || { echo "self-test: one test name in two host logs passed"; failures=$((failures + 1)); }
    [ "$failures" -eq 0 ] || return 1
    echo "self-test: ok"
}

case "${1:-}" in
--self-test) self_test ;;
-h | --help) usage ;;
*)
    [ "$#" -ge 2 ] || { usage >&2; exit 2; }
    compare "$@"
    ;;
esac
