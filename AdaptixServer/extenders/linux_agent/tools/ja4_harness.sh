#!/usr/bin/env bash
# ja4_harness.sh — Gate 1b: JA4 fingerprint verification (H1 transport)
#
# Usage:
#   ja4_harness.sh auto [port] [seconds]     # 自包含：找端口 → 抓包 → 比对（推荐）
#   ja4_harness.sh capture <port> [seconds]   # tshark capture of agent traffic
#   ja4_harness.sh verify <pcap>             # compute JA4 + diff vs baseline
#
# ★ 用 `auto`，不要无参调用本脚本（ISS-093：gate4.sh 曾经就是
#   无参调用 ⇒ 走 usage 分支 exit 2 ⇒ **恒 FAIL**：死门比没门更坏）。
#
# The baseline target is tools/ja4_baseline.txt (curl/OpenSSL line).
# PASS = the captured ClientHello's JA4.1 equals the baseline hash exactly.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
JA4_PY="$HERE/ja4/ja4.py"
BASELINE="$HERE/ja4_baseline.txt"

if [ "$#" -lt 1 ]; then
    echo "usage: ja4_harness.sh auto [port] [seconds] | capture <port> [seconds] | verify <pcap>"
    exit 2
fi

case "$1" in
capture)
    port="${2:?port required}"
    secs="${3:-30}"
    # ★ 不落 /tmp：避免与 p17 T6 的**时间窗**判据纠缠（同批排在 p17 之后的 /tmp 写会假红）
    out="$HERE/.scratch/ja4_capture_$$.pcap"; mkdir -p "$HERE/.scratch"
    echo "[*] capturing ${secs}s on lo port ${port} -> ${out}"
    timeout "${secs}" tshark -i lo -f "tcp port ${port}" -w "$out" >/dev/null 2>&1
    echo "$out"
    ;;
verify)
    pcap="${2:?pcap required}"
    target="$(grep '^curl-openssl:' "$BASELINE" | sed 's/.*JA4.1 = //' | head -1)"
    if [ -z "$target" ]; then
        echo "[FAIL] baseline curl-openssl line not found in $BASELINE"
        exit 1
    fi
    got="$(python3 "$JA4_PY" "$pcap" 2>/dev/null | python3 -c "
import sys, ast
for line in sys.stdin:
    line = line.strip()
    if line.startswith('{'):
        try:
            d = ast.literal_eval(line)
        except Exception:
            continue
        if d.get('JA4.1'):
            print(d['JA4.1'])
            break
")"
    if [ -z "$got" ]; then
        echo "[FAIL] no ClientHello/JA4 found in $pcap"
        exit 1
    fi
    echo "[*] agent   JA4.1 = $got"
    echo "[*] baseline JA4.1 = $target"
    if [ "$got" = "$target" ]; then
        echo "[PASS] JA4 matches the curl/OpenSSL baseline"
        exit 0
    else
        echo "[FAIL] JA4 mismatch"
        exit 1
    fi
    ;;
auto)
    # 自包含入口：自己找端口 → 抓包 → 复用同一份比较器。
    # 端口从运行期 DB 取（TestWSS 的 port_bind），取不到才退回 8443。
    port="${2:-}"
    secs="${3:-30}"
    if [ -z "$port" ]; then
        port="$(python3 - <<'PYEOF' 2>/dev/null
import json, sqlite3
try:
    c = sqlite3.connect("file:/root/cc/run/data/adaptixserver.db?mode=ro", uri=True, timeout=5)
    r = c.execute("select ListenerConfig from Listeners where ListenerName='TestWSS'").fetchone()
    print(int(json.loads(r[0]).get("port_bind") or 8443) if r else 8443)
except Exception:
    print(8443)
PYEOF
)"
        [ -z "$port" ] && port=8443
    fi
    mkdir -p "$HERE/.scratch"
    out="$HERE/.scratch/ja4_auto_$$.pcap"
    echo "[*] auto: capture ${secs}s on lo port ${port} -> ${out}"
    timeout "${secs}" tshark -i lo -f "tcp port ${port}" -w "$out" >/dev/null 2>&1
    bash "$0" verify "$out"; rc=$?
    if [ "$rc" = 0 ]; then rm -f "$out"; else echo "[i] 留证: $out"; fi
    exit "$rc"
    ;;
*)
    echo "unknown subcommand: $1"
    exit 2
    ;;
esac
