#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/sandbox.sh <output directory> <command> [<argument>...]
       tools/acpi/sandbox.sh --self-test

Runs <command> in a sandbox, as the specification has the tools that compile what this repository
did not write run, the pinned iasl first (§8.2): a bubblewrap sandbox with no network, every file
system read-only but <output directory>, a /dev and a /proc of its own, and an environment of PATH
and LC_ALL, with HOME and TMPDIR the output directory. The command starts in the caller's directory, so
that relative paths keep their meaning. The sandbox dies with
this script. Without bwrap nothing runs: there is no unsandboxed fallback.

  --self-test  checks, from inside a sandbox, that a file outside its output directory cannot be
               written, that one inside can, and that no network address can be reached

Exit status: the command's; 1 when bwrap is missing, or bwrap's own when it cannot make the sandbox;
2 on a usage error. --self-test exits 0 when the three checks hold, 1 when one does not.
USAGE
}

# Exits, naming what is missing, when bwrap cannot be run.
require_bubblewrap() {
    if ! command -v bwrap >/dev/null; then
        echo "sandbox: no bwrap on PATH: install bubblewrap (apt install bubblewrap); nothing runs unsandboxed" >&2
        exit 1
    fi
}

# Runs the command $2... in a sandbox whose only writable directory is $1.
run_sandboxed() {
    local output
    output="$(cd "$1" && pwd)"
    shift
    exec bwrap --unshare-all --die-with-parent --new-session \
        --ro-bind / / --dev /dev --proc /proc --bind "$output" "$output" --chdir "$PWD" \
        --clearenv --setenv PATH "$PATH" --setenv LC_ALL "${LC_ALL:-C}" --setenv HOME "$output" \
        --setenv TMPDIR "$output" -- "$@"
}

# Checks the sandbox's three promises from inside one; prints a line per promise.
self_test() {
    local work status
    work="$(mktemp -d)"
    mkdir "$work/output" "$work/outside"
    status=0
    "${BASH_SOURCE[0]}" "$work/output" bash -c '
        failures=0
        if (: >"$1/written") 2>/dev/null; then
            echo "not ok: a file outside the output directory was written"
            failures=$((failures + 1))
        else
            echo "ok: a file outside the output directory cannot be written"
        fi
        if (: >"$HOME/written") 2>/dev/null; then
            echo "ok: a file in the output directory can be written"
        else
            echo "not ok: a file in the output directory cannot be written"
            failures=$((failures + 1))
        fi
        if (exec 3<>/dev/tcp/1.1.1.1/53) 2>/dev/null; then
            echo "not ok: 1.1.1.1:53 was reached"
            failures=$((failures + 1))
        else
            echo "ok: no network address can be reached"
        fi
        exit "$failures"
    ' self-test "$work/outside" || status=1
    rm -rf "$work"
    return "$status"
}

main() {
    case "${1:-}" in
        -h | --help) usage; exit 0 ;;
        --self-test) [[ $# -eq 1 ]] || { usage >&2; exit 2; }; require_bubblewrap; self_test; exit ;;
    esac
    [[ $# -ge 2 && -d "$1" ]] || { usage >&2; exit 2; }
    require_bubblewrap
    run_sandboxed "$@"
}

main "$@"
