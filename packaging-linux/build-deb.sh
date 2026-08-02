#!/usr/bin/env bash

set -euo pipefail

project_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")/.."
  pwd
)"

version="$(
  sed -n \
    's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' \
    "$project_root/CMakeLists.txt" \
  | head -1
)"

if [[ -z "$version" ]]; then
  echo "Could not determine version from CMakeLists.txt." >&2
  exit 1
fi

binary="$project_root/build/scottibyte-assist"
icon="$project_root/assets/scottibyte-assist.png"
control="$project_root/packaging-linux/control"
desktop="$project_root/packaging-linux/scottibyte-assist.desktop"

for required_file in \
  "$binary" \
  "$icon" \
  "$control" \
  "$desktop"
do
  if [[ ! -f "$required_file" ]]; then
    echo "Required file not found: $required_file" >&2
    exit 1
  fi
done

stage="$project_root/packaging-linux/build/root"
output="$project_root/packaging-linux/output"
package="$output/ScottiBYTE-Assist_${version}_amd64.deb"

rm -rf "$project_root/packaging-linux/build"

mkdir -p \
  "$stage/DEBIAN" \
  "$stage/usr/bin" \
  "$stage/usr/share/applications" \
  "$stage/usr/share/icons/hicolor/256x256/apps" \
  "$output"

cp "$control" "$stage/DEBIAN/control"
cp "$binary" "$stage/usr/bin/scottibyte-assist"
cp "$desktop" \
  "$stage/usr/share/applications/scottibyte-assist.desktop"
cp "$icon" \
  "$stage/usr/share/icons/hicolor/256x256/apps/scottibyte-assist.png"

chmod 755 \
  "$stage/usr/bin/scottibyte-assist"

chmod 644 \
  "$stage/DEBIAN/control" \
  "$stage/usr/share/applications/scottibyte-assist.desktop" \
  "$stage/usr/share/icons/hicolor/256x256/apps/scottibyte-assist.png"

find "$stage" \
  -type d \
  -exec chmod 755 {} +

rm -f "$package"

fakeroot dpkg-deb \
  --build \
  "$stage" \
  "$package"

sha256sum "$package"
ls -lh "$package"
