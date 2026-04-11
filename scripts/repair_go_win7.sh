#!/usr/bin/env bash
# Fix: go-win7 clone lacks GOROOT/pkg/include/*.h → assembler errors (textflag.h).
# Run on the teamserver host (root) after pre_install or if Gopher Win7 builds fail.
set -euo pipefail

GOROOT_WIN7="${GOROOT_WIN7:-/usr/lib/go-win7}"
GOROOT_STD="${GOROOT_STD:-/usr/local/go}"

if [[ ! -x "$GOROOT_WIN7/go" ]]; then
  echo "ERROR: $GOROOT_WIN7/go not found. Install go-win7 first (pre_install_linux_all.sh server)."
  exit 1
fi

if [[ ! -f "$GOROOT_STD/pkg/include/textflag.h" ]]; then
  echo "ERROR: $GOROOT_STD/pkg/include/textflag.h missing."
  echo "Install the official Linux amd64 Go tarball to $GOROOT_STD (see pre_install_linux_all.sh)."
  exit 1
fi

mkdir -p "$GOROOT_WIN7/pkg/include"
cp -a "$GOROOT_STD/pkg/include"/* "$GOROOT_WIN7/pkg/include/"
echo "OK: copied asm headers from $GOROOT_STD/pkg/include → $GOROOT_WIN7/pkg/include"
ls -la "$GOROOT_WIN7/pkg/include"
