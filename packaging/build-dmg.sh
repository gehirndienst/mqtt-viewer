#!/usr/bin/env bash
set -euo pipefail

APP="mqtt-viewer"
VERSION=$(grep "version :" meson.build | head -1 | grep -oE "'[0-9][^']+'" | tr -d "'")
BUILD="builddir-release-static"
STAGING="packaging/staging/${APP}.app"
DMG_NAME="packaging/${APP}-${VERSION}-macos.dmg"

for tool in dylibbundler create-dmg; do
    if ! command -v "$tool" &>/dev/null; then
        echo "$tool not found. Install with: brew install $tool" >&2
        exit 1
    fi
done

echo "Building macOS .dmg for ${APP} ${VERSION}..."
rm -rf "packaging/staging"
mkdir -p "${STAGING}/Contents/MacOS"
mkdir -p "${STAGING}/Contents/Frameworks"
mkdir -p "${STAGING}/Contents/Resources"

cp packaging/assets/AppIcon.icns "${STAGING}/Contents/Resources/AppIcon.icns"

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
  <key>CFBundleIconFile</key>     <string>AppIcon</string>
  <key>CFBundleVersion</key>      <string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHumanReadableCopyright</key><string>Copyright 2026 Nikita Smirnov. Apache 2.0</string>
</dict></plist>
PLIST

rm -f "${DMG_NAME}"
create-dmg \
    --volname "MQTT Viewer ${VERSION}" \
    --volicon packaging/assets/AppIcon.icns \
    --background packaging/assets/dmg-background.tiff \
    --window-pos 200 120 \
    --window-size 660 400 \
    --icon-size 100 \
    --text-size 14 \
    --icon "${APP}.app" 165 190 \
    --hide-extension "${APP}.app" \
    --app-drop-link 495 190 \
    "${DMG_NAME}" \
    "packaging/staging"

rm -rf "packaging/staging"
echo "Created ${DMG_NAME}"
