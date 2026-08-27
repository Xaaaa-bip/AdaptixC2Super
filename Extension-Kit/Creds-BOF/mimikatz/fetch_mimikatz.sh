#!/usr/bin/env bash
# Download the official Mimikatz release (Benjamin Delpy / gentilkiwi, CC BY 4.0)
# into this folder's _bin/. Do not commit the PE files.
set -euo pipefail

VER="${MIMIKATZ_VERSION:-2.2.0-20220919}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/_bin"
ZIP_URL="https://github.com/gentilkiwi/mimikatz/releases/download/${VER}/mimikatz_trunk.zip"

mkdir -p "$OUT_DIR"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "[*] Downloading Mimikatz ${VER}"
if command -v curl >/dev/null 2>&1; then
  curl -fsSL -L -o "$TMP/mimikatz.zip" "$ZIP_URL"
elif command -v wget >/dev/null 2>&1; then
  wget -q -O "$TMP/mimikatz.zip" "$ZIP_URL"
else
  echo "[!] Need curl or wget" >&2
  exit 1
fi

if command -v unzip >/dev/null 2>&1; then
  unzip -q -o "$TMP/mimikatz.zip" -d "$TMP/out"
else
  python3 - "$TMP/mimikatz.zip" "$TMP/out" <<'PY'
import sys, zipfile, pathlib
zf = zipfile.ZipFile(sys.argv[1])
dest = pathlib.Path(sys.argv[2])
dest.mkdir(parents=True, exist_ok=True)
zf.extractall(dest)
PY
fi

copied=0
while IFS= read -r -d '' f; do
  case "$f" in
    */x64/mimikatz.exe)
      cp -f "$f" "$OUT_DIR/mimikatz.x64.exe"
      echo "[+] $OUT_DIR/mimikatz.x64.exe"
      copied=$((copied + 1))
      ;;
    */Win32/mimikatz.exe|*/x86/mimikatz.exe)
      cp -f "$f" "$OUT_DIR/mimikatz.x86.exe"
      echo "[+] $OUT_DIR/mimikatz.x86.exe"
      copied=$((copied + 1))
      ;;
  esac
done < <(find "$TMP/out" -type f -name 'mimikatz.exe' -print0)

if [[ ! -f "$OUT_DIR/mimikatz.x64.exe" ]]; then
  echo "[!] mimikatz.x64.exe missing after extract" >&2
  find "$TMP/out" -type f | head -n 50 >&2
  exit 1
fi

echo "[*] Fetched ${copied} PE file(s). Keep _bin/ off git; reload teamserver axscripts after copy."
