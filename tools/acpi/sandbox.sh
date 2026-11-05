#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/sandbox.sh <output directory> <command> [<argument>...]
       tools/acpi/sandbox.sh --self-test

Runs <command> in a sandbox, where the specification runs the tools this repository did not write,
the pinned iasl first (§8.2): a bubblewrap sandbox with no network, every file system read-only but
<output directory>, a /dev and a /proc of its own, and an environment of PATH and LC_ALL alone, with
HOME and TMPDIR the output directory. /run, /tmp, /mnt and the home directory, where the host's
services keep the sockets a read-only file system would leave open (D-Bus, WSL's interop, agents),
are empty in it, but for this repository, the caller's directory, where the command starts, and the
directory of the command itself. The sandbox dies with this script. Without bwrap nothing runs: there
is no unsandboxed fallback.

  --self-test  checks, from inside a sandbox, that a file outside its output directory cannot be
               written, that one inside can, that no socket is left on any file system it sees, and
               that its only network interface is the loopback

Exit status: the command's; 1 when bwrap is missing, or bwrap's own when it cannot make the sandbox;
2 on a usage error. --self-test exits 0 when every check holds, 1 when one does not.
USAGE
}

# Exits, naming what is missing, when bwrap cannot be run.
require_bubblewrap() {
    if ! command -v bwrap >/dev/null; then
        echo "sandbox: no bwrap on PATH: install bubblewrap (apt install bubblewrap); nothing runs unsandboxed" >&2
        exit 1
    fi
}

# Prints the bwrap options that empty the directories holding the host's sockets, the home directory $1 among them.
socket_masks() {
    local directory
    for directory in /run /tmp /mnt "$1"; do
        [[ -n "$directory" && "$directory" != / && -d "$directory" ]] && printf '%s\0' --tmpfs "$directory"
    done
}

# Runs the command $2... in a sandbox whose only writable directory is $1.
run_sandboxed() {
    local output command root
    local -a masks
    output="$(cd "$1" && pwd)"
    shift
    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    if ! command="$(command -v "$1")"; then
        echo "sandbox: no command $1" >&2
        exit 1
    fi
    command="$(dirname "$(readlink -f "$command")")"
    mapfile -d '' masks < <(socket_masks "${HOME:-}")
    exec bwrap --unshare-all --die-with-parent --new-session \
        --ro-bind / / --dev /dev --proc /proc "${masks[@]}" \
        --ro-bind "$root" "$root" --ro-bind "$PWD" "$PWD" --ro-bind "$command" "$command" \
        --bind "$output" "$output" --chdir "$PWD" \
        --clearenv --setenv PATH "$PATH" --setenv LC_ALL "${LC_ALL:-C}" --setenv HOME "$output" \
        --setenv TMPDIR "$output" -- "$@"
}

# Checks the sandbox's promises from inside one, writing to the repository's root as the outside; prints a line per
# promise.
self_test() {
    local work status outside
    work="$(mktemp -d)"
    outside="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/.sandbox-self-test"
    status=0
    "${BASH_SOURCE[0]}" "$work" bash -c '
        failures=0
        if (: >"$1") 2>/dev/null; then
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
        socket="$(find / \( -path /proc -o -path /sys \) -prune -o -type s -print -quit 2>/dev/null)"
        if [[ -n "$socket" ]]; then
            echo "not ok: the socket $socket is left in the sandbox"
            failures=$((failures + 1))
        else
            echo "ok: no socket is left on any file system the sandbox sees"
        fi
        interfaces="$(awk '"'"'NR > 2 { sub(":", "", $1); printf "%s ", $1 }'"'"' /proc/net/dev)"
        if [[ "$interfaces" != "lo " ]]; then
            echo "not ok: the network interfaces ${interfaces% } are in the sandbox"
            failures=$((failures + 1))
        else
            echo "ok: the only network interface is the loopback"
        fi
        exit "$failures"
    ' self-test "$outside" || status=1
    rm -rf "$work" "$outside"
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
