#!/usr/bin/env bash
# compile_check.sh — 只编译、不落盘到 dist/run 的静态验证（P1 起每次改 src_agent 都跑）
#
# 为什么需要它：agent 的 ELF 是**服务器运行时按载荷现场编译**的
# （pl_main.go: NativeSrcDir="src_agent/agent"），改了源码不编译一次就发现不了
# 语法/头文件错误，而错误要等到真正 gen_agent 时才以"编译失败"的形式暴露。
# 本脚本用**与 pl_main.go / deploy.sh 完全相同的编译参数**全量过一遍：
#   ① src_agent/agent/*.c  —— x86_64 (musl-gcc) 与 aarch64 (aarch64-linux-gnu-gcc) 两套
#   ② src_agent/bofs/*.c   —— deploy.sh 的 BOF 参数
# 输出到临时目录，随时删除；-**绝不**写 dist/ 或 run/（那两个目录在跑 soak/门禁时
# 被动用，见 ISS-018）。
#
# 用法: bash tools/compile_check.sh [--quiet]     退出码 0 = 全绿
set -u
cd "$(dirname "$0")/.." || exit 1
SRC=src_agent/agent
BOFS=src_agent/bofs
OUT=$(mktemp -d /tmp/axcc.XXXXXX) || exit 1
trap 'rm -rf "$OUT"' EXIT
QUIET=${1:-}

# 与 pl_main.go nativeCFlags() 逐字一致
BASE="-std=gnu11 -Os -U_FORTIFY_SOURCE -fno-stack-protector -fno-builtin \
-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-cast-function-type \
-I src_agent/mbedtls/include -I src_agent/mbedtls \
-DMBEDTLS_CONFIG_FILE=<mbedtls_config_agent.h>"

command -v python3 >/dev/null && python3 tools/gen_stub_headers.py "$OUT/hdr" \
    || { echo "  [ERR ] 桩头文件生成失败（需要 python3）"; exit 1; }

fail=0; warn=0; nfile=0
run_cc() {   # run_cc <tag> <compiler> <flags> <src> <outbase>
    local tag=$1 cc=$2 fl=$3 src=$4 ob=$5 log
    log=$OUT/$(echo "$tag" | tr '/ ' '__').log
    if ! $cc -c -o "$OUT/$ob.o" $fl "$src" 2>"$log"; then
        echo "  [ERR ] $tag"
        sed -n '1,25p' "$log" | sed 's/^/         /'
        fail=$((fail+1)); return
    fi
    local w; w=$(grep -c 'warning:' "$log" 2>/dev/null); w=${w:-0}
    warn=$((warn+w))
    if [ "$w" != "0" ] && [ "$QUIET" != "--quiet" ]; then
        echo "  [warn] $tag  ($w)"
        grep 'warning:' "$log" | sed -n '1,8p' | sed 's/^/         /'
    fi
    nfile=$((nfile+1))
}

for arch in x86_64 aarch64; do
    case $arch in
        x86_64)  CC=musl-gcc;                 DEF=-DARCH_X86_64 ;;
        aarch64) CC=aarch64-linux-gnu-gcc;    DEF=-DARCH_AARCH64 ;;
    esac
    command -v $CC >/dev/null || { echo "  [SKIP] $CC 不存在，跳过 $arch"; continue; }
    echo "== agent $arch ($CC) =="
    for f in "$SRC"/*.c; do
        [ -e "$f" ] || continue
        run_cc "$arch/$(basename "$f")" "$CC" "$BASE $DEF -I $SRC -I $OUT/hdr" "$f" "$(basename "$f" .c)_$arch"
    done
done

echo "== bofs (deploy.sh 参数) =="
for f in "$BOFS"/*.c; do
    [ -e "$f" ] || continue
    run_cc "bof/$(basename "$f")" gcc \
        "-Os -fPIC -fno-stack-protector -I $SRC -include bof_api.h" "$f" "$(basename "$f" .c)_bof"
done

echo
echo "编译通过 $nfile 个文件，警告 $warn 条，失败 $fail 个"
[ "$fail" = "0" ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $((fail > 0))
