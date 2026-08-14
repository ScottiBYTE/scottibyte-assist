#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-windows"
DEPLOY="$ROOT/deploy-windows"
UCRT_BIN="/ucrt64/bin"
GST_PLUGINS="/ucrt64/lib/gstreamer-1.0"
GST_SCANNER="/ucrt64/libexec/gstreamer-1.0/gst-plugin-scanner.exe"

PLUGINS=(
  libgstapp.dll
  libgstcoreelements.dll
  libgstaudioconvert.dll
  libgstaudioresample.dll
  libgstvolume.dll
  libgstopus.dll
  libgstrtp.dll
  libgstrtpmanager.dll
  libgstvideoconvertscale.dll
  libgstvpx.dll
  libgstwasapi2.dll
)

echo "== Configure Release =="
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release

echo "== Build Assist =="
cmake --build "$BUILD" -j"$(nproc)"

echo "== Create clean deployment =="
rm -rf "$DEPLOY"
mkdir -p "$DEPLOY"

cp \
  "$BUILD/scottibyte-assist.exe" \
  "$DEPLOY/scottibyte-assist.exe"

echo "== Deploy Qt runtime =="
"$UCRT_BIN/windeployqt.exe" \
  --release \
  "$DEPLOY/scottibyte-assist.exe"

echo "== Deploy required GStreamer plugins =="
mkdir -p "$DEPLOY/lib/gstreamer-1.0"

for plugin in "${PLUGINS[@]}"; do
  if [[ ! -f "$GST_PLUGINS/$plugin" ]]; then
    echo "ERROR: Missing GStreamer plugin: $plugin"
    exit 1
  fi

  cp \
    "$GST_PLUGINS/$plugin" \
    "$DEPLOY/lib/gstreamer-1.0/"
done

echo "== Deploy GStreamer plugin scanner =="
mkdir -p "$DEPLOY/libexec/gstreamer-1.0"

cp \
  "$GST_SCANNER" \
  "$DEPLOY/libexec/gstreamer-1.0/"

echo "== Resolve runtime DLL dependencies =="

python3 - "$DEPLOY" "$UCRT_BIN" <<'PY'
from pathlib import Path
import re
import shutil
import subprocess
import sys

deploy = Path(sys.argv[1])
ucrt = Path(sys.argv[2])

def packaged_names():
    return {
        p.name.lower()
        for p in deploy.rglob("*")
        if p.is_file()
    }

def binaries():
    return [
        p
        for p in deploy.rglob("*")
        if p.is_file()
        and p.suffix.lower() in {".dll", ".exe"}
    ]

def dependencies(path):
    result = subprocess.run(
        ["objdump", "-p", str(path)],
        capture_output=True,
        text=True,
        errors="ignore",
    )

    return re.findall(
        r"DLL Name:\s*(\S+)",
        result.stdout,
        re.IGNORECASE,
    )

round_number = 0

while True:
    round_number += 1
    present = packaged_names()
    copied = []

    for binary in binaries():
        for dll in dependencies(binary):
            key = dll.lower()

            if key in present:
                continue

            source = ucrt / dll

            if source.is_file():
                destination = deploy / dll

                if not destination.exists():
                    shutil.copy2(
                        source,
                        destination,
                    )
                    copied.append(dll)
                    present.add(key)

    if not copied:
        break

    print(
        f"Dependency pass {round_number}: "
        f"copied {len(copied)} DLL(s)"
    )

    for dll in sorted(
        set(copied),
        key=str.lower,
    ):
        print(f"  {dll}")

present = packaged_names()
unresolved = {}

for binary in binaries():
    for dll in dependencies(binary):
        key = dll.lower()

        if key in present:
            continue

        source = ucrt / dll

        if source.is_file():
            continue

        unresolved.setdefault(
            dll,
            set(),
        ).add(str(binary))

print()
print("== Dependency closure ==")

if not unresolved:
    print(
        "PASS: Every dependency is packaged "
        "or resolved from UCRT64."
    )
else:
    print(
        "Dependencies not supplied by UCRT64 "
        "(normally Windows system DLLs):"
    )

    for dll in sorted(
        unresolved,
        key=str.lower,
    ):
        print(f"  {dll}")
PY

echo
echo "== Deployment complete =="
du -sh "$DEPLOY"
