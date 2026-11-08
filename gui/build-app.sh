#!/bin/bash
# Builds "Re-ARMed Mods.app" (arm64) into gui/build/, with rearmed-mods.dylib (../build.sh)
# inside it: the app installs the library into RS Re-ARMed's mods folder.
#   ./build-app.sh           build
#   ./build-app.sh --open    build and open
#   ./build-app.sh --zip     build and make build/Re-ARMed-Mods-<version>.zip, the release download
set -euo pipefail
cd "$(dirname "$0")"
version=$(cat ../VERSION)

../build.sh
swift build -c release --arch arm64
BIN="$(swift build -c release --arch arm64 --show-bin-path)/RSModsMac"

APP="build/Re-ARMed Mods.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Re-ARMed Mods"
cp ../build/rearmed-mods.dylib "$APP/Contents/Resources/"
cp icon/AppIcon.icns "$APP/Contents/Resources/"  # icon/make_icon.py draws it
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Re-ARMed Mods</string>
    <key>CFBundleDisplayName</key><string>Re-ARMed Mods</string>
    <key>CFBundleIdentifier</key><string>io.github.glebb.rearmed-mods</string>
    <key>CFBundleExecutable</key><string>Re-ARMed Mods</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
codesign --force --sign - "$APP"
echo "Built $APP ($version)"

case "${1:-}" in
  --open) open "$APP" ;;
  --zip)
    zip="build/Re-ARMed-Mods-$version.zip"
    rm -f "$zip"
    ditto -c -k --keepParent "$APP" "$zip"
    echo "Release: $zip" ;;
esac
