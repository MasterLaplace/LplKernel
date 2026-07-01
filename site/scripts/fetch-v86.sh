#!/bin/sh
# Fetch the v86 runtime blobs into site/public/v86/.
# v86 is MIT-licensed (https://github.com/copy/v86). We vendor a pinned release
# rather than committing the binaries. Run this before `npm run build` locally;
# CI runs it automatically.
set -e

# Pin to a specific v86 npm release for reproducibility.
V86_VERSION="${V86_VERSION:-0.5.419}"
# BIOS blobs are not published to npm; take them from the git tag matching the release.
V86_BIOS_REF="${V86_BIOS_REF:-v${V86_VERSION}}"

DEST="$(dirname "$0")/../public/v86"
NPM_BASE="https://cdn.jsdelivr.net/npm/v86@${V86_VERSION}/build"
BIOS_BASE="https://raw.githubusercontent.com/copy/v86/${V86_BIOS_REF}/bios"

mkdir -p "$DEST"

echo "[fetch-v86] downloading v86@${V86_VERSION} runtime into $DEST"
for f in libv86.js v86.wasm; do
  curl -fsSL "$NPM_BASE/$f" -o "$DEST/$f"
done

echo "[fetch-v86] downloading BIOS blobs (ref ${V86_BIOS_REF})"
for f in seabios.bin vgabios.bin; do
  if ! curl -fsSL "$BIOS_BASE/$f" -o "$DEST/$f"; then
    echo "[fetch-v86] tag ${V86_BIOS_REF} missing $f, falling back to master"
    curl -fsSL "https://raw.githubusercontent.com/copy/v86/master/bios/$f" -o "$DEST/$f"
  fi
done

echo "[fetch-v86] done:"
ls -lh "$DEST"
