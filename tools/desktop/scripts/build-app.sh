#!/bin/sh
set -eu

# Build a runnable, ad-hoc-signed .app bundle around the SwiftPM executable.
# Usage: scripts/build-app.sh [--debug] [--open] [--universal]

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CONFIG=release
OPEN_APP=0
UNIVERSAL=0

for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=debug ;;
        --open) OPEN_APP=1 ;;
        --universal) UNIVERSAL=1 ;;
        *) echo "usage: $0 [--debug] [--open] [--universal]" >&2; exit 2 ;;
    esac
done

ARCH_FLAGS=""
if [ "$UNIVERSAL" -eq 1 ]; then
    ARCH_FLAGS="--arch arm64 --arch x86_64"
fi

VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$ROOT/Resources/Info.plist" 2>/dev/null || echo "0.1.0")
BUILD=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$ROOT/Resources/Info.plist" 2>/dev/null || echo "1")

# Ask SwiftPM where the product lands. A single-arch build uses .build/<config>;
# a universal build uses a different Products path on current toolchains.
BIN_DIR=$(swift build -c "$CONFIG" $ARCH_FLAGS --package-path "$ROOT" --show-bin-path)
swift build -c "$CONFIG" $ARCH_FLAGS --package-path "$ROOT"
BIN="$BIN_DIR/HelikonCompanion"
[ -x "$BIN" ] || {
    echo "missing executable: $BIN" >&2
    exit 1
}

APP="$ROOT/dist/Helikon Companion.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"

rm -rf "$APP"
mkdir -p "$MACOS" "$RESOURCES"
cp "$BIN" "$MACOS/HelikonCompanion"
chmod 755 "$MACOS/HelikonCompanion"

RESOURCE_BUNDLE="$BIN_DIR/HelikonCompanion_HelikonCompanion.bundle"
if [ -d "$RESOURCE_BUNDLE" ]; then
    cp -R "$RESOURCE_BUNDLE" "$RESOURCES/"
fi

SPLASH_ASSET="$RESOURCES/HelikonCompanion_HelikonCompanion.bundle/Contents/Resources/helikon-os-banner.png"
if [ ! -f "$SPLASH_ASSET" ]; then
    echo "missing welcome banner in the app bundle: $SPLASH_ASSET" >&2
    exit 1
fi

if [ -f "$ROOT/Resources/AppIcon.png" ]; then
    ICONSET="$ROOT/.build/AppIcon.iconset"
    rm -rf "$ICONSET"
    mkdir -p "$ICONSET"
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$ROOT/Resources/AppIcon.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2))
        sips -z "$double" "$double" "$ROOT/Resources/AppIcon.png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$RESOURCES/AppIcon.icns"
fi

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Helikon Companion</string>
    <key>CFBundleExecutable</key>
    <string>HelikonCompanion</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>os.helikon.companion</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Helikon Companion</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${BUILD}</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.music</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Helikon-OS project. GPL-2.0-or-later.</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
PLIST

if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
fi

echo "$APP"
if [ "$OPEN_APP" -eq 1 ]; then
    open "$APP"
fi
