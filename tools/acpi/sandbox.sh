#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/sandbox.sh <output directory> <command> [<argument>...]
       tools/acpi/sandbox.sh --self-test

Runs <command> in a sandbox, where the specification runs the tools this repository did not write,
the pinned iasl first (§8.2): a bubblewrap sandbox with no network, every file system read-only but
<output directory>, a /dev and a /proc of its own, and an environment of PATH and LC_ALL alone, with
HOME and TMPDIR the output directory.

  - /run, /tmp, /mnt and the home directory, where the host's services keep the sockets a read-only
    file system would leave open (D-Bus, WSL's interop, agents), are empty in it, but for this
    repository and the directory of the command, bound back read-only;
  - socket() is refused, as any system call of another architecture: a network namespace does not
    cut AF_VSOCK, the channel to the hypervisor;
  - the command reads an empty standard input, and no descriptor above 2 the caller left open;
  - the caller stands in this repository, where the command starts, so that relative paths keep
    their meaning; a command whose directory is the root, a masked directory or holds one is refused.

The sandbox dies with this script. Without bwrap nothing runs: there is no unsandboxed fallback.

  --self-test  checks, from inside a sandbox, that files outside its output directory, in the
               repository and in /var/tmp, are read-only, that one inside can be written, that no
               socket is left on any file system it sees, that its only network interface is the
               loopback, that no socket can be made, and that a descriptor the caller left open is
               closed

Exit status: the command's; 1 when bwrap is missing, when the caller or the command stands where the
sandbox would uncover a masked directory, or bwrap's own when it cannot make the sandbox; 2 on a usage
error. --self-test exits 0 when every check holds, 1 when one does not.
USAGE
}

# Exits, naming what is missing, when bwrap cannot be run.
require_bubblewrap() {
    if ! command -v bwrap >/dev/null; then
        echo "sandbox: no bwrap on PATH: install bubblewrap (apt install bubblewrap); nothing runs unsandboxed" >&2
        exit 1
    fi
}

# Prints, one per line, the directories the sandbox empties because the host's services keep their sockets there,
# the home directory $1 among them.
masked_directories() {
    local directory
    for directory in /run /tmp /mnt "$1"; do
        [[ -n "$directory" && "$directory" != / && -d "$directory" ]] && echo "$directory"
    done
}

# Succeeds when binding the directory $1 back into the sandbox would bring back a masked directory $2...: when it is
# the root, one of them, or holds one.
uncovers_a_mask() {
    local directory="$1" mask
    shift
    [[ "$directory" == / ]] && return 0
    for mask in "$@"; do
        [[ "$mask/" == "$directory/"* ]] && return 0
    done
    return 1
}

# Exits, naming the directory, when one the sandbox binds back would bring back a masked directory: the
# repository $1, the command's directory $2, the caller's directory, which must stand in the repository.
require_safe_bindings() {
    local root="$1" command_directory="$2" directory
    local -a masks
    mapfile -t masks < <(masked_directories "${HOME:-}")
    if [[ "$PWD/" != "$root/"* ]]; then
        echo "sandbox: the caller's directory $PWD is not in the repository $root: run from it" >&2
        exit 1
    fi
    for directory in "$root" "$command_directory"; do
        if uncovers_a_mask "$directory" "${masks[@]}"; then
            echo "sandbox: binding $directory back would bring back the sockets of a masked directory" >&2
            exit 1
        fi
    done
}

# Prints the seccomp program, classic BPF for x86-64, that refuses socket() with EPERM, and any system call of
# another architecture or of the x32 ABI: the network namespace does not cut AF_VSOCK, the channel to the hypervisor.
socket_filter() {
    printf '\x20\x00\x00\x00\x04\x00\x00\x00'
    printf '\x15\x00\x00\x04\x3e\x00\x00\xc0'
    printf '\x20\x00\x00\x00\x00\x00\x00\x00'
    printf '\x35\x00\x02\x00\x00\x00\x00\x40'
    printf '\x15\x00\x01\x00\x29\x00\x00\x00'
    printf '\x06\x00\x00\x00\x00\x00\xff\x7f'
    printf '\x06\x00\x00\x00\x01\x00\x05\x00'
}

# Closes every descriptor above 2 the caller left open, which would let the command write outside the sandbox.
close_inherited_descriptors() {
    local descriptor
    for descriptor in /proc/$$/fd/*; do
        descriptor="${descriptor##*/}"
        [[ "$descriptor" -gt 2 && "$descriptor" -ne 255 ]] && eval "exec $descriptor>&-"
    done
    return 0
}

# Runs the command $2... in a sandbox whose only writable directory is $1.
run_sandboxed() {
    local output command root mask filter
    local -a masks=()
    output="$(cd "$1" && pwd)"
    shift
    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    if ! command="$(command -v "$1")"; then
        echo "sandbox: no command $1" >&2
        exit 1
    fi
    command="$(dirname "$(readlink -f "$command")")"
    require_safe_bindings "$root" "$command"
    while IFS= read -r mask; do
        masks+=(--tmpfs "$mask")
    done < <(masked_directories "${HOME:-}")
    close_inherited_descriptors
    filter="$(mktemp)"
    socket_filter >"$filter"
    exec 3<"$filter"
    rm -f "$filter"
    exec bwrap --unshare-all --die-with-parent --new-session --seccomp 3 \
        --ro-bind / / --dev /dev --proc /proc "${masks[@]}" \
        --ro-bind "$root" "$root" --ro-bind "$command" "$command" --bind "$output" "$output" --chdir "$PWD" \
        --clearenv --setenv PATH "$PATH" --setenv LC_ALL "${LC_ALL:-C}" --setenv HOME "$output" \
        --setenv TMPDIR "$output" -- "$@" </dev/null
}

# Checks the sandbox's promises from inside one, given a descriptor 7 open on a file outside it, and two files
# outside it to write: one in the repository, one in /var/tmp. Prints a line per promise.
self_test() {
    local work status root
    work="$(mktemp -d)"
    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    status=0
    (
        cd "$root"
        exec 7>>"$work/inherited"
        "${BASH_SOURCE[0]}" "$work" bash -c "$(self_test_probes)" self-test "$root/.sandbox-self-test" \
            "/var/tmp/.sandbox-self-test-$$"
    ) || status=1
    if [[ -s "$work/inherited" ]]; then
        echo "not ok: a descriptor the caller left open was written through"
        status=1
    fi
    rm -rf "$work" "$root/.sandbox-self-test" "/var/tmp/.sandbox-self-test-$$"
    return "$status"
}

# Prints the script self_test runs in the sandbox, its arguments the files outside it to write.
self_test_probes() {
    cat <<'PROBES'
failures=0
fail() { echo "not ok: $1"; failures=$((failures + 1)); }
for outside in "$@"; do
    if (: >"$outside") 2>"$HOME/written"; then
        fail "$outside, outside the output directory, was written"
    elif ! grep -q 'Read-only file system' "$HOME/written"; then
        fail "$outside, outside the output directory, was refused for another reason: $(cat "$HOME/written")"
    else
        echo "ok: $outside, outside the output directory, is read-only"
    fi
done
if (: >"$HOME/inside") 2>/dev/null; then
    echo "ok: a file in the output directory can be written"
else
    fail "a file in the output directory cannot be written"
fi
find / \( -path /proc -o -path /sys \) -prune -o -type s -print >"$HOME/sockets" 2>"$HOME/find"
if [[ ! -f "$HOME/sockets" ]]; then
    fail "the file systems were not searched for sockets"
elif [[ -s "$HOME/sockets" ]]; then
    fail "the socket $(head -n 1 "$HOME/sockets") is left in the sandbox"
else
    echo "ok: no socket is left on any file system the sandbox sees"
fi
interfaces="$(awk 'NR > 2 { sub(":", "", $1); printf "%s ", $1 }' /proc/net/dev)"
if [[ "$interfaces" != "lo " ]]; then
    fail "the network interfaces ${interfaces% } are in the sandbox"
else
    echo "ok: the only network interface is the loopback"
fi
(exec 4<>/dev/tcp/127.0.0.1/9) 2>"$HOME/socket"
if grep -q 'Operation not permitted' "$HOME/socket"; then
    echo "ok: no socket can be made, a vsock to the hypervisor included"
else
    fail "a socket could be made: $(cat "$HOME/socket")"
fi
if (echo inherited >&7) 2>/dev/null; then
    fail "the descriptor 7 the caller left open is still open"
else
    echo "ok: no descriptor the caller left open is open"
fi
exit "$failures"
PROBES
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
