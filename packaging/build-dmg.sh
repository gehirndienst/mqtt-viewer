#!/usr/bin/env bash
set -euo pipefail

APP="mqtt-viewer"
VERSION=$(grep "version :" meson.build | head -1 | grep -oE "'[0-9][^']+'" | tr -d "'")
BUILD="builddir-release-static"
STAGING="packaging/staging/${APP}.app"
DMG_NAME="packaging/${APP}-${VERSION}-macos.dmg"

if ! command -v dylibbundler &>/dev/null; then
    echo "dylibbundler not found. Install with: brew install dylibbundler" >&2
    exit 1
fi

echo "Building macOS .dmg for ${APP} ${VERSION}..."
rm -rf "packaging/staging"
mkdir -p "${STAGING}/Contents/MacOS"
mkdir -p "${STAGING}/Contents/Frameworks"

cp "${BUILD}/${APP}" "${STAGING}/Contents/MacOS/${APP}"
chmod 755 "${STAGING}/Contents/MacOS/${APP}"

dylibbundler \
    --bundle-deps \
    --overwrite-dir \
    --fix-file "${STAGING}/Contents/MacOS/${APP}" \
    --dest-dir "${STAGING}/Contents/Frameworks/" \
    --install-path "@executable_path/../Frameworks/"

RPATH="@executable_path/../Frameworks/"
BIN="${STAGING}/Contents/MacOS/${APP}"
COUNT=$(otool -l "$BIN" | grep -c "path $RPATH" || true)
for ((i = 1; i < COUNT; i++)); do
    install_name_tool -delete_rpath "$RPATH" "$BIN"
done
if ((COUNT > 1)); then
    codesign --force --sign - "$BIN"
fi

cat > "${STAGING}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key>    <string>${APP}</string>
  <key>CFBundleIdentifier</key>   <string>com.gehirndienst.mqtt-viewer</string>
  <key>CFBundleName</key>         <string>MQTT Viewer</string>
  <key>CFBundleVersion</key>      <string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHumanReadableCopyright</key><string>Copyright 2026 Nikita Smirnov. Apache 2.0</string>
</dict></plist>
PLIST

hdiutil create \
    -volname "MQTT Viewer ${VERSION}" \
    -srcfolder "packaging/staging" \
    -ov -format UDZO \
    "${DMG_NAME}"

rm -rf "packaging/staging"
echo "Created ${DMG_NAME}"
