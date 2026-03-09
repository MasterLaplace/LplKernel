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
            REALTIME_MODE=1
            export GRAPHICS_MODE=1
            ;;
        --server)
            REALTIME_MODE=0
            export GRAPHICS_MODE=0
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

# APIC policy is profile-driven now.
# - Backend is always built/enabled.
# - Ownership handoff is enabled in client profile and disabled in server profile.
APIC_TIMER_BACKEND=1
if [ "$REALTIME_MODE" -eq 1 ]; then
    APIC_TIMER_OWNER=1
    IOAPIC_KEYBOARD_OWNER=1
else
    APIC_TIMER_OWNER=0
    IOAPIC_KEYBOARD_OWNER=0
fi

. ./config.sh

# Auto-clean when the build mode changes to avoid stale object files.
LAST_MODE_FILE=".last_build_mode"
CURRENT_MODE="REALTIME=$REALTIME_MODE GRAPHICS=${GRAPHICS_MODE:-0} APIC_BACKEND=$APIC_TIMER_BACKEND APIC_OWNER=$APIC_TIMER_OWNER IOAPIC_KBD_OWNER=$IOAPIC_KEYBOARD_OWNER APIC_SMOKE=$APIC_SMOKE_TEST_PERIODIC_MODE"

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
        (cd "$PROJECT" && DESTDIR="$SYSROOT" GRAPHICS_MODE="$GRAPHICS_MODE" REALTIME_MODE="$REALTIME_MODE" APIC_TIMER_BACKEND="$APIC_TIMER_BACKEND" APIC_TIMER_OWNER="$APIC_TIMER_OWNER" IOAPIC_KEYBOARD_OWNER="$IOAPIC_KEYBOARD_OWNER" APIC_SMOKE_TEST_PERIODIC_MODE="$APIC_SMOKE_TEST_PERIODIC_MODE" compiledb $MAKE -j"$JOBS" install)
        jq --indent 1 'map(.arguments += ["-resource-dir=/nonexistent"])' \
            $PROJECT/compile_commands.json > tmp                          \
            && mv tmp $PROJECT/compile_commands.json
    else
        (cd "$PROJECT" && DESTDIR="$SYSROOT" GRAPHICS_MODE="$GRAPHICS_MODE" REALTIME_MODE="$REALTIME_MODE" APIC_TIMER_BACKEND="$APIC_TIMER_BACKEND" APIC_TIMER_OWNER="$APIC_TIMER_OWNER" IOAPIC_KEYBOARD_OWNER="$IOAPIC_KEYBOARD_OWNER" APIC_SMOKE_TEST_PERIODIC_MODE="$APIC_SMOKE_TEST_PERIODIC_MODE" $MAKE -j"$JOBS" install)
    fi
done
