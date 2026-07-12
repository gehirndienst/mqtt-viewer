#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

APP="mqtt-viewer"
VERSION=$(grep -m1 -E "version\s*:" meson.build | grep -oE "[0-9]+\.[0-9]+\.[0-9]+")
ARCH=$(dpkg --print-architecture)
BUILD="builddir-release-static"
STAGING="packaging/staging/deb"
DEB_NAME="packaging/${APP}_${VERSION}_${ARCH}.deb"

if [ -z "${VERSION}" ]; then
    echo "Could not extract version from meson.build" >&2
    exit 1
fi

echo "Building .deb for ${APP} ${VERSION} (${ARCH})..."
trap 'rm -rf "packaging/staging"' EXIT
rm -rf "packaging/staging"
mkdir -p "${STAGING}/DEBIAN"
mkdir -p "${STAGING}/usr/bin"
mkdir -p "${STAGING}/usr/share/doc/${APP}"

cp "${BUILD}/${APP}" "${STAGING}/usr/bin/${APP}"
chmod 755 "${STAGING}/usr/bin/${APP}"

INSTALLED_SIZE=$(du -sk "${STAGING}/usr" | cut -f1)

cat > "${STAGING}/DEBIAN/control" <<CTRL
Package: ${APP}
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: Nikita Smirnov <nktsmirnov@gmail.com>
Installed-Size: ${INSTALLED_SIZE}
Section: net
Priority: optional
Depends: libc6, libmosquitto1 (>= 2.0), libcjson1 (>= 1.7), libx11-6, libgl1, libxcursor1, libxi6, libxinerama1, libxrandr2
Description: Real-time MQTT topic viewer
 A native MQTT client with a tree-first UI for monitoring and inspecting
 MQTT broker traffic in real time. Features topic filtering, message
 inspection (JSON/Text/Hex), publish panel, and broker profile management
CTRL

cat > "${STAGING}/usr/share/doc/${APP}/copyright" <<COPYRIGHT
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: mqtt-viewer
Upstream-Contact: Nikita Smirnov <nktsmirnov@gmail.com>
Source: https://github.com/gehirndienst/mqtt-viewer
License: Apache-2.0
COPYRIGHT

find "${STAGING}" -type d -exec chmod 755 {} +
chmod 644 "${STAGING}/DEBIAN/control" "${STAGING}/usr/share/doc/${APP}/copyright"
chmod 755 "${STAGING}/usr/bin/${APP}"

dpkg-deb --root-owner-group --build "${STAGING}" "${DEB_NAME}"
echo "Created ${DEB_NAME}"
