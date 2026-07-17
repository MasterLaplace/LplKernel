#!/bin/sh
set -e

JOBS=1
USE_COMPILEDB=0
REALTIME_MODE=1          # default: client/realtime build (Free-List PMM)
APIC_SMOKE_TEST_PERIODIC_MODE=0

for arg in "$@"; do
    case "$arg" in
        --graphics)
            export GRAPHICS_MODE=1
            ;;
        --text)
            export GRAPHICS_MODE=0
            ;;
        --realtime)
            REALTIME_MODE=1
            ;;
        --client)
            export REALTIME_MODE=1
            export GRAPHICS_MODE=1
            ;;
        --server)
            export REALTIME_MODE=0
            export GRAPHICS_MODE=0
            ;;
        --azerty)
            export KEYBOARD_LAYOUT=fr
            ;;
        --qwerty)
            export KEYBOARD_LAYOUT=us
            ;;
        --compile-db)
            USE_COMPILEDB=1
            ;;
        *)
            case "$arg" in
                --*)
                    echo "Unknown option: $arg" >&2
                    exit 1
                    ;;
                *)
                    JOBS="$arg"
                    ;;
            esac
            ;;
    esac
done

. ./config.sh

# Auto-clean when the build mode changes to avoid stale object files.
LAST_MODE_FILE=".last_build_mode"
# ENABLE_LIBENGINE is part of the fingerprint: it flips the -DLPL_PLUGIN_UNAVAILABLE
# compile flag, which make cannot see on its own (it tracks source mtimes, not flags).
# Without this, client_app.o / kernel.o keep a stale "no engine module" stub baked in
# after the engine appears (or vice-versa). Including it forces an auto-clean on flip.
CURRENT_MODE="REALTIME=$REALTIME_MODE GRAPHICS=${GRAPHICS_MODE:-0} APIC_SMOKE=$APIC_SMOKE_TEST_PERIODIC_MODE KEYBOARD=${KEYBOARD_LAYOUT:-us} ENGINE=${ENABLE_LIBENGINE:-0}"

if [ -f "$LAST_MODE_FILE" ]; then
    LAST_MODE=$(cat "$LAST_MODE_FILE")
    if [ "$LAST_MODE" != "$CURRENT_MODE" ]; then
        echo "Build mode changed ($LAST_MODE -> $CURRENT_MODE), cleaning..."
        for PROJECT in $PROJECTS; do
            (cd "$PROJECT" && $MAKE clean)
        done
        rm -rf sysroot iso lpl.iso
    fi
fi
echo "$CURRENT_MODE" > "$LAST_MODE_FILE"

. ./headers.sh

echo "Building with graphics mode: $GRAPHICS_MODE"

for PROJECT in $PROJECTS; do
    if [ "$USE_COMPILEDB" -eq 1 ]; then
        (cd "$PROJECT" && DESTDIR="$SYSROOT" GRAPHICS_MODE="$GRAPHICS_MODE" REALTIME_MODE="$REALTIME_MODE" APIC_SMOKE_TEST_PERIODIC_MODE="$APIC_SMOKE_TEST_PERIODIC_MODE" KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-us}" compiledb $MAKE -j"$JOBS" install)
        jq --indent 1 'map(.arguments += ["-resource-dir=/nonexistent"])' \
            $PROJECT/compile_commands.json > tmp                          \
            && mv tmp $PROJECT/compile_commands.json
    else
        (cd "$PROJECT" && DESTDIR="$SYSROOT" GRAPHICS_MODE="$GRAPHICS_MODE" REALTIME_MODE="$REALTIME_MODE" APIC_SMOKE_TEST_PERIODIC_MODE="$APIC_SMOKE_TEST_PERIODIC_MODE" KEYBOARD_LAYOUT="${KEYBOARD_LAYOUT:-us}" $MAKE -j"$JOBS" install)
    fi
done
