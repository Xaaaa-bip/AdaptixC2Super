#!/usr/bin/env bash
# deploy_matrix.sh — Phase 3: package the built matrix into ~/cc/run/matrix
# (rsync-free; plain cp + chmod 600 + sha256 listing)

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
MATRIX_ROOT="${MATRIX_ROOT:-$HOME/cc/run/matrix}"
DEST="$HOME/cc/run/matrix"

[ -d "$MATRIX_ROOT" ] || { echo "matrix root $MATRIX_ROOT not built — run build_matrix.sh first"; exit 1; }

mkdir -p "$DEST"
cp -rf "$MATRIX_ROOT"/exploits "$MATRIX_ROOT"/implants "$MATRIX_ROOT"/ko "$MATRIX_ROOT"/ebpf "$MATRIX_ROOT"/matrix_manifest.json "$DEST"/ 2>/dev/null
find "$DEST" -type f -exec chmod 600 {} \;

echo "[*] deployed to $DEST"
echo "[*] sha256 listing:"
( cd "$DEST" && find . -type f -exec sha256sum {} \; | sort -k2 )
