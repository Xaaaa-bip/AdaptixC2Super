#!/bin/bash
set -e

PROJECT_DIR="/mnt/c/Users/15305/Downloads/AdaptixC2 - 副本/AdaptixServer"
cd "$PROJECT_DIR"

export CGO_ENABLED=1
export GOEXPERIMENT=jsonv2,greenteagc
export GOOS=linux
export GOARCH=amd64

echo "========================================="
echo " Building AdaptixC2 Server + All Plugins"
echo " Go version: $(go version)"
echo "========================================="

# ---- Build Server ----
echo ""
echo "[1/13] Building AdaptixServer..."
cd "$PROJECT_DIR"
go build -ldflags="-s -w" -o adaptixserver_linux_amd64 .
echo "  -> adaptixserver_linux_amd64 built"

# ---- Build Plugins ----
build_plugin() {
    local name="$1"
    local dir="$2"
    local so_name="$3"
    local src="${4:-.}"
    local gowork="${5:-on}"
    echo ""
    echo "[*] Building $name..."
    cd "$PROJECT_DIR/$dir"
    if [ "$gowork" = "off" ]; then
        GOWORK=off go build -buildmode=plugin -ldflags="-s -w" -o "$so_name" "$src"
    else
        go build -buildmode=plugin -ldflags="-s -w" -o "$so_name" "$src"
    fi
    echo "  -> $name done: $so_name"
}

# Listeners (in go.work)
build_plugin "beacon_listener_http"  "extenders/beacon_listener_http"  "listener_beacon_http.so"
build_plugin "beacon_listener_smb"   "extenders/beacon_listener_smb"   "listener_beacon_smb.so"
build_plugin "beacon_listener_tcp"   "extenders/beacon_listener_tcp"   "listener_beacon_tcp.so"
build_plugin "beacon_listener_dns"   "extenders/beacon_listener_dns"   "beacon_listener_dns.so"
build_plugin "gopher_listener_tcp"   "extenders/gopher_listener_tcp"   "listener_gopher_tcp.so"
build_plugin "listener_kharon_http"  "extenders/listener_kharon_http"  "listener_kharon_http.so"  "./src_server/"

# Agents (in go.work)
build_plugin "beacon_agent"          "extenders/beacon_agent"          "agent_beacon.so"
build_plugin "gopher_agent"          "extenders/gopher_agent"          "agent_gopher.so"
build_plugin "agent_kharon"          "extenders/agent_kharon"          "agent_kharon.so"          "./src_server/"

# Services (standalone modules - need GOWORK=off)
build_plugin "notifier"              "extenders/notifier"              "notifier.so"              "."  "off"
build_plugin "webdelivery"           "extenders/webdelivery"           "webdelivery.so"           "."  "off"
build_plugin "vnc"                   "extenders/vnc"                   "vnc.so"                   "."  "off"

# ---- Copy to dist/ ----
echo ""
echo "[*] Copying files to dist/ directories..."

copy_dist() {
    local dir="$1"
    local so="$2"
    mkdir -p "$PROJECT_DIR/$dir/dist"
    cp "$PROJECT_DIR/$dir/config.yaml" "$PROJECT_DIR/$dir/dist/" 2>/dev/null || true
    cp "$PROJECT_DIR/$dir/$so" "$PROJECT_DIR/$dir/dist/" 2>/dev/null || true
    if [ -f "$PROJECT_DIR/$dir/ax_config.axs" ]; then
        cp "$PROJECT_DIR/$dir/ax_config.axs" "$PROJECT_DIR/$dir/dist/" 2>/dev/null || true
    fi
}

copy_dist "extenders/beacon_listener_http"  "listener_beacon_http.so"
copy_dist "extenders/beacon_listener_smb"   "listener_beacon_smb.so"
copy_dist "extenders/beacon_listener_tcp"   "listener_beacon_tcp.so"
copy_dist "extenders/beacon_listener_dns"   "beacon_listener_dns.so"
copy_dist "extenders/gopher_listener_tcp"   "listener_gopher_tcp.so"
copy_dist "extenders/listener_kharon_http"  "listener_kharon_http.so"
copy_dist "extenders/beacon_agent"          "agent_beacon.so"
copy_dist "extenders/gopher_agent"          "agent_gopher.so"
copy_dist "extenders/agent_kharon"          "agent_kharon.so"
copy_dist "extenders/notifier"              "notifier.so"
copy_dist "extenders/webdelivery"           "webdelivery.so"
copy_dist "extenders/vnc"                   "vnc.so"

# Copy web viewer for vnc
if [ -d "$PROJECT_DIR/extenders/vnc/web" ]; then
    cp -r "$PROJECT_DIR/extenders/vnc/web" "$PROJECT_DIR/extenders/vnc/dist/" 2>/dev/null || true
fi

# Copy VNC stream agent payloads used by beacon/gopher vnc auto-start
if [ -d "$PROJECT_DIR/extenders/vnc/agent" ]; then
    cp -r "$PROJECT_DIR/extenders/vnc/agent" "$PROJECT_DIR/extenders/vnc/dist/" 2>/dev/null || true
fi

echo ""
echo "========================================="
echo " Build Complete!"
echo "========================================="
echo " Server: $PROJECT_DIR/adaptixserver_linux_amd64"
echo " Plugins:"
find "$PROJECT_DIR/extenders" -name "*.so" -type f 2>/dev/null | while read f; do
    echo "   $f ($(du -h "$f" | cut -f1))"
done
