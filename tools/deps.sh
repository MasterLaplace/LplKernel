#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/deps.sh [status|lock|fetch|add <Name> <url>]

The other Laplace repositories this one builds with, and the exact commits it was
tested with. Versions and minimums are not decided here: each repository writes its
version in its config.h, and the compiler refuses a dependency that is too old.

  status            each locked repository: found where, which version, how far from the lock (default)
  lock              write the commits and versions found now into DEPENDENCIES.lock
  fetch             clone every locked commit next to this repository (what CI runs)
  add <Name> <url>  start tracking a repository, then run lock

A repository is looked for at $<NAME>_ROOT (uppercase), else at ../<Name>;
<NAME>_ROOT=none leaves it out. Its version is read from the header that defines
<PREFIX>_NAME "<Name>".
EOF
}

REPOSITORY="$(git rev-parse --show-toplevel)"
LOCK_FILE="$REPOSITORY/DEPENDENCIES.lock"

root_of() {
    local variable
    variable="$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')_ROOT"
    if [ -n "${!variable:-}" ]; then
        [ "${!variable}" = "none" ] || printf '%s' "${!variable}"
        return 0
    fi
    printf '%s' "$(cd "$REPOSITORY/.." && pwd)/$1"
}

display_path() {
    local parent
    parent="$(cd "$REPOSITORY/.." && pwd)"
    case "$1" in "$parent"/*) printf '../%s' "${1#"$parent"/}" ;; *) printf '%s' "$1" ;; esac
}

version_in() {
    local root="$1" name="$2" header prefix
    header="$(git -C "$root" grep --untracked -l -E "^#[[:space:]]*define [A-Z0-9_]+_NAME \"$name\"" -- '*.h' | head -n1)" || true
    [ -n "$header" ] || return 0
    prefix="$(sed -nE "s/^#[[:space:]]*define ([A-Z0-9_]+)_NAME \"$name\".*/\1/p" "$root/$header" | head -n1)"
    awk -v p="$prefix" '
        $1 == "#define" && $2 == p "_VERSION_MAJOR" { major = $3 }
        $1 == "#define" && $2 == p "_VERSION_MINOR" { minor = $3 }
        $1 == "#define" && $2 == p "_VERSION_PATCH" { patch = $3 }
        END { if (major != "") print major "." minor "." patch }' "$root/$header"
}

is_present() {
    [ -n "$1" ] && { [ -d "$1/.git" ] || [ -f "$1/.git" ]; }
}

lock_lines() {
    [ -f "$LOCK_FILE" ] || return 0
    grep -vE '^[[:space:]]*(#|$)' "$LOCK_FILE"
}

print_status() {
    printf '  %-13s %-34s %-8s %-8s %s\n' "repository" "found" "HEAD" "lock" "state"
    local name version commit url root found head state
    while read -r name version commit url; do
        root="$(root_of "$name")"
        if ! is_present "$root"; then
            printf '  %-13s %-34s %-8s %-8s %s\n' "$name" "-" "-" "${commit:0:7}" "absent, built without it"
            continue
        fi
        found="$(version_in "$root" "$name")"
        head="$(git -C "$root" rev-parse HEAD)"
        state="ok"
        [ -n "$found" ] || state="no config.h names it"
        [ -z "$(git -C "$root" status --porcelain --untracked-files=no)" ] || state="$state, uncommitted changes"
        if [ "$head" != "$commit" ]; then
            if git -C "$root" merge-base --is-ancestor "$commit" "$head" 2>/dev/null; then
                ahead="$(git -C "$root" rev-list --count "$commit..$head")"
                state="$state, $ahead commit$([ "$ahead" -eq 1 ] || echo s) past the lock"
            else
                state="$state, not the locked commit"
            fi
        fi
        printf '  %-13s %-34s %-8s %-8s %s\n' "$name" "${found:-?} at $(display_path "$root")" "${head:0:7}" \
            "${commit:0:7}" "$state"
    done < <(lock_lines)
}

write_lock() {
    local name version commit url root problems=0 lines=""
    while read -r name version commit url; do
        root="$(root_of "$name")"
        if ! is_present "$root"; then
            printf '%s is not at %s: clone it before locking.\n' "$name" "$(display_path "$(root_of "$name")")" >&2
            problems=1; continue
        fi
        if [ -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]; then
            printf '%s has uncommitted changes: a lock names commits, commit them first.\n' "$name" >&2
            problems=1; continue
        fi
        if [ -z "$(git -C "$root" branch -r --contains HEAD 2>/dev/null)" ]; then
            printf '%s is at %s, which no remote branch holds: CI could not fetch it, push it first.\n' \
                "$name" "$(git -C "$root" rev-parse --short=7 HEAD)" >&2
            problems=1; continue
        fi
        version="$(version_in "$root" "$name")"
        lines+="$(printf '%-13s %-7s %s %s' "$name" "${version:--}" \
            "$(git -C "$root" rev-parse HEAD)" "$url")"$'\n'
    done < <(lock_lines)
    [ "$problems" -eq 0 ] || return 1
    {
        echo "# The exact commits this commit builds and gates with. Refresh with tools/deps.sh lock."
        echo "# <name> <version> <commit> <url>"
        printf '%s' "$lines"
    } > "$LOCK_FILE"
    printf 'wrote %s\n' "$(display_path "$LOCK_FILE")"
}

fetch_locked() {
    [ -f "$LOCK_FILE" ] || { echo "no DEPENDENCIES.lock: nothing to fetch" >&2; return 1; }
    local name version commit url target
    while read -r name version commit url; do
        target="$(root_of "$name")"
        [ -n "$target" ] || continue
        if [ ! -d "$target" ]; then
            git init --quiet "$target"
            git -C "$target" remote add origin "$url"
            git -C "$target" fetch --quiet --depth 1 origin "$commit"
        else
            git -C "$target" fetch --quiet origin "$commit"
        fi
        git -C "$target" -c advice.detachedHead=false checkout --quiet "$commit"
        printf '%-13s %-7s %s\n' "$name" "$version" "${commit:0:7}"
    done < <(lock_lines)
}

command="${1:-status}"
case "$command" in
    -h|--help|help) usage ;;
    status) print_status ;;
    lock) write_lock ;;
    fetch) fetch_locked ;;
    add)
        [ $# -eq 3 ] || { usage >&2; exit 2; }
        printf '%-13s %-7s %s %s\n' "$2" "-" "-" "$3" >> "$LOCK_FILE"
        write_lock
        ;;
    *) printf 'unknown command: %s\n\n' "$command" >&2; usage >&2; exit 2 ;;
esac
