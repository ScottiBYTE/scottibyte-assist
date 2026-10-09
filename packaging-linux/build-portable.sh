#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONTAINER="ScottiBYTE-Build"
BUILD_ROOT="/opt/scottibyte-build"
OUTPUT="$PROJECT_ROOT/packaging-linux/output"

echo "=== ScottiBYTE Assist Portable Build ==="

if ! incus info "$CONTAINER" >/dev/null 2>&1; then
    echo "ERROR: Build container $CONTAINER does not exist."
    exit 1
fi

if [[ "$(incus list "$CONTAINER" -c s --format csv)" != "RUNNING" ]]; then
    incus start "$CONTAINER"
fi

echo
echo "=== Synchronizing source ==="

incus exec "$CONTAINER" -- bash -c '
set -e

mkdir -p /opt/scottibyte-build

tar \
    --exclude=.git \
    --exclude=build \
    --exclude=dist \
    --exclude=packaging-linux/build \
    --exclude=packaging-linux/output \
    -C /mnt/scottibyte-assist \
    -cf - . |
tar -C /opt/scottibyte-build -xf -
'

echo
echo "=== Building inside Ubuntu 24.04 ==="

incus exec "$CONTAINER" -- bash -c '
set -euo pipefail

ROOT=/opt/scottibyte-build
VERSION=$(awk "/^[[:space:]]*VERSION[[:space:]]+[0-9]/{print \$2; exit}" "$ROOT/CMakeLists.txt")

cmake -S "$ROOT" -B "$ROOT/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT/build" \
    --target scottibyte-assist \
    -j 8

BIN="$ROOT/build/scottibyte-assist"

echo
echo "=== Verifying Qt compatibility ==="

if readelf --version-info "$BIN" | grep -Eq "Qt_6\.(5|6|7|8|9|10|11|12)"; then
    echo "ERROR: Binary requires Qt newer than Ubuntu 24.04."
    exit 1
fi

echo
echo "=== Creating Debian package ==="

STAGE=/tmp/scottibyte-portable-package
DEST="$STAGE/usr/lib/scottibyte-assist"
PACKAGE="/tmp/ScottiBYTE-Assist_${VERSION}_amd64.deb"

rm -rf "$STAGE"

mkdir -p \
    "$STAGE/DEBIAN" \
    "$STAGE/usr/bin" \
    "$DEST" \
    "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/256x256/apps"

cp "$BIN" "$STAGE/usr/bin/scottibyte-assist"

cp /usr/local/lib/x86_64-linux-gnu/libwebrtc-audio-processing-1.so.3 "$DEST/"

for lib in \
    libabsl_bad_optional_access.so.20220623 \
    libabsl_strings.so.20220623 \
    libabsl_throw_delegate.so.20220623 \
    libabsl_strings_internal.so.20220623 \
    libabsl_raw_logging_internal.so.20220623
do
    cp -L "/usr/lib/x86_64-linux-gnu/$lib" "$DEST/"
    patchelf --force-rpath --set-rpath "\$ORIGIN" "$DEST/$lib"
done

patchelf --force-rpath --set-rpath "\$ORIGIN" \
    "$DEST/libwebrtc-audio-processing-1.so.3"

patchelf --force-rpath \
    --set-rpath "\$ORIGIN/../lib/scottibyte-assist" \
    "$STAGE/usr/bin/scottibyte-assist"

cp "$ROOT/packaging-linux/scottibyte-assist.desktop" \
    "$STAGE/usr/share/applications/"

cp "$ROOT/assets/scottibyte-assist.png" \
    "$STAGE/usr/share/icons/hicolor/256x256/apps/"

cp "$ROOT/packaging-linux/control" "$STAGE/DEBIAN/control"

sed -i "s/^Version: .*/Version: $VERSION/" \
    "$STAGE/DEBIAN/control"

sed -i \
    "s/libqt6core6,/libqt6core6t64 | libqt6core6,/; \
     s/libqt6gui6,/libqt6gui6t64 | libqt6gui6,/; \
     s/libqt6widgets6,/libqt6widgets6t64 | libqt6widgets6,/; \
     s/libqt6network6,/libqt6network6t64 | libqt6network6,/; \
     s/libqt6dbus6,/libqt6dbus6t64 | libqt6dbus6,/" \
    "$STAGE/DEBIAN/control"

chmod 755 "$STAGE/usr/bin/scottibyte-assist"
chmod 755 "$DEST/"*.so.*

find "$STAGE" -type d -exec chmod 755 {} +

fakeroot dpkg-deb --build "$STAGE" "$PACKAGE"

echo
echo "=== Package created ==="
ls -lh "$PACKAGE"
'

echo
echo "=== Copying installer to Mondo-2 ==="

mkdir -p "$OUTPUT"

VERSION="$(awk '/^[[:space:]]*VERSION[[:space:]]+[0-9]/{print $2; exit}' "$PROJECT_ROOT/CMakeLists.txt")"

incus file pull \
    "$CONTAINER/tmp/ScottiBYTE-Assist_${VERSION}_amd64.deb" \
    "$OUTPUT/"

echo
echo "=== Portable installer ==="
ls -lh "$OUTPUT/ScottiBYTE-Assist_${VERSION}_amd64.deb"

echo
echo "=== SHA-256 ==="
sha256sum "$OUTPUT/ScottiBYTE-Assist_${VERSION}_amd64.deb"

echo
echo "=== BUILD COMPLETE ==="
