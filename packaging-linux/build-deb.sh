#!/usr/bin/env bash
set -euo pipefail

project_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.."
  pwd
)"

version="$(
  awk '/^[[:space:]]*VERSION[[:space:]]+[0-9]/{print $2; exit}' \
    "$project_root/CMakeLists.txt"
)"

if [[ -z "$version" ]]; then
  echo "Could not determine version from CMakeLists.txt." >&2
  exit 1
fi

binary="$project_root/build/scottibyte-assist"
icon="$project_root/assets/scottibyte-assist.png"
control="$project_root/packaging-linux/control"
desktop="$project_root/packaging-linux/scottibyte-assist.desktop"
webrtc_apm="/usr/local/lib/x86_64-linux-gnu/libwebrtc-audio-processing-1.so.3"

for required_file in \
  "$binary" \
  "$icon" \
  "$control" \
  "$desktop" \
  "$webrtc_apm"
do
  if [[ ! -f "$required_file" ]]; then
    echo "Required file not found: $required_file" >&2
    exit 1
  fi
done

if ! command -v patchelf >/dev/null 2>&1; then
  echo "patchelf is required to build the package." >&2
  exit 1
fi

stage="$project_root/packaging-linux/build/root"
output="$project_root/packaging-linux/output"
package="$output/ScottiBYTE-Assist_${version}_amd64.deb"

rm -rf "$project_root/packaging-linux/build"

mkdir -p \
  "$stage/DEBIAN" \
  "$stage/usr/bin" \
  "$stage/usr/lib/scottibyte-assist" \
  "$stage/usr/share/applications" \
  "$stage/usr/share/icons/hicolor/256x256/apps" \
  "$output"

cp "$control" \
  "$stage/DEBIAN/control"

sed -i \
  "s/^Version: .*/Version: $version/" \
  "$stage/DEBIAN/control"

cp "$binary" \
  "$stage/usr/bin/scottibyte-assist"

cp "$webrtc_apm" \
  "$stage/usr/lib/scottibyte-assist/libwebrtc-audio-processing-1.so.3"

cp "$desktop" \
  "$stage/usr/share/applications/scottibyte-assist.desktop"

cp "$icon" \
  "$stage/usr/share/icons/hicolor/256x256/apps/scottibyte-assist.png"

patchelf \
  --set-rpath '$ORIGIN/../lib/scottibyte-assist' \
  "$stage/usr/bin/scottibyte-assist"

chmod 755 \
  "$stage/usr/bin/scottibyte-assist" \
  "$stage/usr/lib/scottibyte-assist/libwebrtc-audio-processing-1.so.3"

chmod 644 \
  "$stage/DEBIAN/control" \
  "$stage/usr/share/applications/scottibyte-assist.desktop" \
  "$stage/usr/share/icons/hicolor/256x256/apps/scottibyte-assist.png"

find "$stage" \
  -type d \
  -exec chmod 755 {} +

echo
echo "=== Packaged executable RPATH ==="
patchelf --print-rpath "$stage/usr/bin/scottibyte-assist"

echo
echo "=== Packaged APM resolution ==="
ldd "$stage/usr/bin/scottibyte-assist" \
  | grep -E 'webrtc-audio-processing|absl'

rm -f "$package"

fakeroot dpkg-deb \
  --build \
  "$stage" \
  "$package"

echo
echo "=== SHA-256 ==="
sha256sum "$package"

echo
echo "=== Package ==="
ls -lh "$package"
