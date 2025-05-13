#!/bin/sh
set -e

JOBS=1
if [ "$#" -ge 1 ]; then
    JOBS=$1
fi

. ./headers.sh

for PROJECT in $PROJECTS; do
    (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE -j"$JOBS" install)
done
