#!/bin/sh
set -eu

# Build a drag-to-Applications DMG for the Helikon Companion shell.
# Usage: scripts/build-dmg.sh [--universal] [--debug]
# The app is ad-hoc signed; this script does not notarize it.

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/../.." && pwd)
APP="$ROOT/dist/Helikon Companion.app"
STAGE="$ROOT/dist/dmg"
CONFIG=release
UNIVERSAL=0

for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=debug ;;
        --universal) UNIVERSAL=1 ;;
        *) echo "usage: $0 [--debug] [--universal]" >&2; exit 2 ;;
    esac
done

BUILD_ARGS=""
if [ "$CONFIG" = "debug" ]; then
    BUILD_ARGS="--debug"
fi
if [ "$UNIVERSAL" -eq 1 ]; then
    BUILD_ARGS="$BUILD_ARGS --universal"
fi

"$ROOT/scripts/build-app.sh" $BUILD_ARGS

VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$ROOT/Resources/Info.plist" 2>/dev/null || echo "0.1.0")
DMG="$ROOT/dist/Helikon-Companion-$VERSION-alpha2.dmg"

test -d "$APP" || {
    echo "missing app bundle: $APP" >&2
    exit 1
}

rm -rf "$STAGE"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/Helikon Companion.app"
ln -s /Applications "$STAGE/Applications"
cp "$REPO/LICENSE" "$REPO/NOTICE" "$STAGE/"

cat > "$STAGE/Read Me.txt" <<'README'
Helikon Companion 0.1.0
Alpha 2.0.0 — shell preview

This is a development preview of the macOS companion. It contains the owner
welcome banner and an empty shell only. It does not read or write an iPod
volume, use the network, scan a library, transfer files or install anything.

Requirements
------------
macOS 14 or newer.

Install
-------
Drag "Helikon Companion.app" to Applications. On first launch, right-click the
app and choose Open, or use System Settings > Privacy & Security > Open Anyway.
This build is ad-hoc signed and not notarized, so Gatekeeper will warn. Do not
disable Gatekeeper globally.

Scope and limits
----------------
- No device access, library metadata, transfer, podcast, audiobook, scrobbler,
  installer or settings functionality.
- No test target; no device run; no support matrix.
- The app is a visual preview, not a working companion.

License
-------
GPL-2.0-or-later. See LICENSE and NOTICE in this disk image. No private key or
device backup is included.
README

codesign --verify --deep --strict "$APP"
if [ "$UNIVERSAL" -eq 1 ]; then
    ARCHS=$(lipo -archs "$APP/Contents/MacOS/HelikonCompanion")
    case "$ARCHS" in
        *arm64*x86_64*|*x86_64*arm64*) : ;;
        *)
            echo "error: app is not universal (found: $ARCHS)" >&2
            exit 1
            ;;
    esac
fi

rm -f "$DMG"
hdiutil create -volname "Helikon Companion" -srcfolder "$STAGE" \
    -ov -format UDZO "$DMG" >/dev/null
hdiutil verify "$DMG" >/dev/null

echo "DMG:  $DMG"
echo "size: $(wc -c < "$DMG" | tr -d ' ') bytes"
echo "sha256: $(shasum -a 256 "$DMG" | awk '{print $1}')"
