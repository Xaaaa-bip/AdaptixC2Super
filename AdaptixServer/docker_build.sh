#!/bin/bash
# Build AdaptixC2 server + plugins for Ubuntu 20.04 (GLIBC 2.31)
# Run: wsl bash AdaptixServer/docker_build.sh

set -e

PROJECT="/mnt/c/Users/15305/Downloads/AdaptixC2 - 副本/AdaptixServer"

echo "=== Building AdaptixC2 in Ubuntu 20.04 Docker (GLIBC 2.31) ==="

docker run --rm \
    -v "$PROJECT:/build" \
    -w /build \
    ubuntu:20.04 bash -c '
set -e

echo "[1/4] Installing build tools..."
apt-get update -qq && apt-get install -y -qq wget gcc g++ make > /dev/null 2>&1

echo "[2/4] Installing Go 1.24..."
wget -q https://go.dev/dl/go1.24.11.linux-amd64.tar.gz -O /tmp/go.tar.gz
tar -C /usr/local -xzf /tmp/go.tar.gz
export PATH="/usr/local/go/bin:$PATH"
echo "   Go version: $(go version)"

echo "[3/4] Building server + all plugins..."
export CGO_ENABLED=1
export GOEXPERIMENT=jsonv2,greenteagc
export GOOS=linux
export GOARCH=amd64

# Build server
echo "   Building AdaptixServer..."
go build -ldflags="-s -w" -o adaptixserver_linux_amd64 .
echo "   -> Server done ($(du -h adaptixserver_linux_amd64 | cut -f1))"

# Build plugins function
build_plugin() {
    local name="$1"
    local dir="$2"
    local so_name="$3"
    local src="${4:-.}"
    local gowork="${5:-on}"
    echo "   Building $name..."
    cd "/build/$dir"
    if [ "$gowork" = "off" ]; then
        GOWORK=off go build -buildmode=plugin -ldflags="-s -w" -o "$so_name" "$src"
    else
        go build -buildmode=plugin -ldflags="-s -w" -o "$so_name" "$src"
    fi
    echo "     -> $so_name ($(du -h "$so_name" | cut -f1))"
    cd /build
}

# Listeners
build_plugin "beacon_listener_http"  "extenders/beacon_listener_http"  "listener_beacon_http.so"
build_plugin "beacon_listener_smb"   "extenders/beacon_listener_smb"   "listener_beacon_smb.so"
build_plugin "beacon_listener_tcp"   "extenders/beacon_listener_tcp"   "listener_beacon_tcp.so"
build_plugin "beacon_listener_dns"   "extenders/beacon_listener_dns"   "beacon_listener_dns.so"
build_plugin "gopher_listener_tcp"   "extenders/gopher_listener_tcp"   "listener_gopher_tcp.so"
build_plugin "listener_kharon_http"  "extenders/listener_kharon_http"  "listener_kharon_http.so"  "./src_server/"

# Agents
build_plugin "beacon_agent"          "extenders/beacon_agent"          "agent_beacon.so"
build_plugin "gopher_agent"          "extenders/gopher_agent"          "agent_gopher.so"
build_plugin "agent_kharon"          "extenders/agent_kharon"          "agent_kharon.so"          "./src_server/"

# Services (GOWORK=off)
build_plugin "notifier"              "extenders/notifier"              "notifier.so"              "."  "off"
build_plugin "webdelivery"           "extenders/webdelivery"           "webdelivery.so"           "."  "off"
build_plugin "vnc"                   "extenders/vnc"                   "vnc.so"                   "."  "off"

# Copy to dist/
echo "[4/4] Copying to dist/ dirs..."
for dir in extenders/*/; do
    mkdir -p "/build/$dir/dist"
    cp "/build/$dir/config.yaml" "/build/$dir/dist/" 2>/dev/null || true
    if [ -f "/build/$dir/ax_config.axs" ]; then
        cp "/build/$dir/ax_config.axs" "/build/$dir/dist/" 2>/dev/null || true
    fi
    for so in "/build/$dir"*.so; do
        [ -f "$so" ] && cp "$so" "/build/$dir/dist/" 2>/dev/null || true
    done
done

# Copy vnc web files
if [ -d "/build/extenders/vnc/web" ]; then
    cp -r /build/extenders/vnc/web /build/extenders/vnc/dist/ 2>/dev/null || true
fi

echo ""
echo "=== Build Complete ==="
echo "Server: /build/adaptixserver_linux_amd64"
find /build/extenders -name "*.so" -type f | while read f; do
    echo "  Plugin: $f ($(du -h "$f" | cut -f1))"
done
'
