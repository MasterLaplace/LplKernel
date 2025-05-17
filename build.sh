#!/bin/sh
set -e

JOBS=1
USE_COMPILEDB=0

for arg in "$@"; do
    case "$arg" in
        --compile-db)
            USE_COMPILEDB=1
            ;;
        *)
            JOBS="$arg"
            ;;
    esac
done

. ./headers.sh

for PROJECT in $PROJECTS; do
    if [ "$USE_COMPILEDB" -eq 1 ]; then
        (cd "$PROJECT" && DESTDIR="$SYSROOT" compiledb $MAKE -j"$JOBS" install)
        jq --indent 1 'map(.arguments += ["-resource-dir=/nonexistent"])' \
            $PROJECT/compile_commands.json > tmp                          \
            && mv tmp $PROJECT/compile_commands.json
    else
        (cd "$PROJECT" && DESTDIR="$SYSROOT" $MAKE -j"$JOBS" install)
    fi
done
