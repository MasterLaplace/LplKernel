#!/bin/sh
set -e
. ./iso.sh "$@"

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom lpl.iso -m 256M -serial stdio
