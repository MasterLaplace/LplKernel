#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/acpi/check-portable.sh [<source>...]

Checks that the ACPI layer holds no architecture: each source (by default kernel/kernel/acpi/*.c)
compiles with the host's gcc for long mode, freestanding, against libk's headers, and includes no
header of kernel/include/ but those of kernel/include/kernel/acpi/.

Exit status: 0 when every source passes, 1 when one does not compile or includes another header,
2 on a usage error.
USAGE
}

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly FLAGS=(-std=gnu11 -ffreestanding -m64 -mno-red-zone -mcmodel=kernel -fno-pic -nostdinc
                -I "$ROOT/libc/include" -I "$ROOT/kernel/include" -Wall -Wextra -Werror)

[[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && { usage; exit 0; }

sources=("$@")
[[ ${#sources[@]} -eq 0 ]] && sources=("$ROOT"/kernel/kernel/acpi/*.c)

failures=0
for source in "${sources[@]}"; do
    [[ -f "$source" ]] || { usage >&2; exit 2; }
    if ! gcc "${FLAGS[@]}" -c "$source" -o /dev/null; then
        echo "not ok $source: does not compile for long mode"
        failures=$((failures + 1))
        continue
    fi
    outside="$(gcc "${FLAGS[@]}" -MM "$source" | tr ' \\' '\n\n' | grep -E "^$ROOT/kernel/include/|^kernel/include/" |
               grep -vE '/kernel/include/kernel/acpi/' || true)"
    if [[ -n "$outside" ]]; then
        echo "not ok $source: includes $(echo "$outside" | paste -sd ' ')"
        failures=$((failures + 1))
        continue
    fi
    echo "ok $source"
done
[[ "$failures" -eq 0 ]]
