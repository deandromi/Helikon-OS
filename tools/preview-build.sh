#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Assemble one day's preview payload.
#
#   ./tools/preview-build.sh [OUTPUT_DIR]
#
# Builds the read-only 6G firmware image and the Disk Mode image from the
# current tree, then lays them out with a hash list and a README that says, in
# the first paragraph, that this is preview code with no device claim.
#
# Environment:
#   CROSS           toolchain prefix                     (default arm-elf-eabi-)
#   TOOLCHAIN_PATH  prepended to PATH for the build      (default empty)
#   EXTRA_CFLAGS    extra compiler flags                 (default empty)
#   PREVIEW_DATE    date stamp for the files             (default today, UTC)
#   PREVIEW_VERSION image version word                   (default date, digits)
#
# The CI workflow sets CROSS=arm-none-eabi- and EXTRA_CFLAGS=-Wno-error: the
# distro ARM compiler is newer than the pinned Rockbox one, and a preview build
# must not fail on a warning that the toolchain of record does not emit. The
# size and symbol checks still run, so a broken image still fails.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$ROOT/dist/preview"}
CROSS=${CROSS:-arm-elf-eabi-}
PREVIEW_DATE=${PREVIEW_DATE:-$(date -u +%Y-%m-%d)}
PREVIEW_VERSION=${PREVIEW_VERSION:-$(date -u +%Y%m%d)}
EXTRA=${EXTRA_CFLAGS:-}
PREVIEW_COMMIT=${PREVIEW_COMMIT:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)}
PREVIEW_LOG=${PREVIEW_LOG:-$(git -C "$ROOT" log --oneline -15 2>/dev/null || echo unknown)}

if [ -n "${TOOLCHAIN_PATH:-}" ]; then
    PATH="$TOOLCHAIN_PATH:$PATH"
    export PATH
fi
export CROSS

if ! command -v "${CROSS}gcc" >/dev/null 2>&1; then
    echo "preview-build: ${CROSS}gcc not found in PATH" >&2
    echo "preview-build: set TOOLCHAIN_PATH or CROSS" >&2
    exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT/payload-6g" "$OUT/payload-6g-diskmode"

echo "== firmware 6G (read-only)"
make -C "$ROOT/firmware" BOARD=6g VERSION="$PREVIEW_VERSION" \
    EXTRA_CFLAGS="$EXTRA" image >/dev/null
cp "$ROOT"/build/firmware/6g/out/* "$OUT/payload-6g/"

echo "== firmware 6G (Disk Mode)"
make -C "$ROOT/firmware" BOARD=6g DISKMODE=1 VERSION="$PREVIEW_VERSION" \
    EXTRA_CFLAGS="$EXTRA" image >/dev/null
cp "$ROOT"/build/firmware/6g-diskmode/out/* "$OUT/payload-6g-diskmode/"

cat > "$OUT/README-PREVIEW.md" <<EOF
# Helikon-OS preview build — $PREVIEW_DATE

> **This is preview code.** It is built from the tip of \`main\` and it has not
> run on any device. It is not a release, it is not supported, and it may not
> boot, may not read your library and may behave differently tomorrow. There is
> no Alpha or beta promise here. Use it to look at, not as an update you rely on:
> there is no supported install path for these files, and the section below says
> why.

## What is inside

| Path | What it is |
|---|---|
| \`payload-6g/\` | The read-only 6G firmware image, its manifest, boot state and the public half of the key it was signed with |
| \`payload-6g-diskmode/\` | The same for the Disk Mode image (USB mass storage, no HFS+ mount) |

Both payloads are raw images with a \`firmware.manifest\`, a \`bootstate\` and a
\`SHA256SUMS\` list. They are signed with a **test key** this build generated; the
public half is in \`test-public-key.txt\`. No release key was used, and nothing in
this release verifies the signature: a loader image that accepts these files has
to be built with the matching private key, which is not shipped. Treat the files
as build output you can inspect, not as an installable update.

## Installing (there is no supported install for this artifact)

Preview firmware cannot be installed from this release. The images are signed
with a per-build test key, and a loader image can only accept them when both were
built together, from the same commit, on the same machine. Build locally instead:

1. Read [\`bootloader/LOADER_TEST.md\`](https://github.com/deandromi/Helikon-OS/blob/main/bootloader/LOADER_TEST.md)
   and follow it. The loader test image is installed through DFU only — never with
   \`--single\`, which destroys the Apple NOR path.
2. Take a verified backup of the device first, and read a file back from it to
   prove the backup works. Never test on the only copy of a library.
3. Stage the payload as the runbook describes, then let the loader verify the
   manifest and jump.

If you already run a loader you built yourself, the payload layout is the one in
this directory: \`firmware.img\`, \`firmware.manifest\` and \`bootstate\`. If any of
that is not true for you, do not flash anything: read
[\`bootloader/DFU.md\`](https://github.com/deandromi/Helikon-OS/blob/main/bootloader/DFU.md)
and [\`docs/device-matrix.md\`](https://github.com/deandromi/Helikon-OS/blob/main/docs/device-matrix.md)
first, and remember that no 6G run is recorded for this build.

## Checks

\`\`\`sh
shasum -a 256 -c SHA256SUMS        # macOS
sha256sum -c SHA256SUMS            # Linux
\`\`\`

## Provenance

- Commit: \`$PREVIEW_COMMIT\`
- Built: $PREVIEW_DATE (UTC), image version word $PREVIEW_VERSION
- Host suites: the workflow that produced this release ran
  \`tools/host-tests.sh\` before it built anything; the log is on the run
EOF

cat > "$OUT/CHANGELOG.md" <<EOF
# Changes up to $PREVIEW_DATE

\`\`\`
$PREVIEW_LOG
\`\`\`
EOF

(
    cd "$OUT"
    find . -type f ! -name SHA256SUMS | sort | while read -r file; do
        if command -v shasum >/dev/null 2>&1; then
            shasum -a 256 "$file"
        else
            sha256sum "$file"
        fi
    done > SHA256SUMS
)

echo "== $OUT"
find "$OUT" -type f | sort
