#!/bin/bash
# deploy.sh — 把源码树的改动同步到服务器真正读取的运行目录。
#
# 为什么必须有这个脚本（ISS-018）：
#   pl_main.go 里 `NativeSrcDir = "src_agent/agent"` 是**相对路径**，服务器进程的
#   cwd 是 /root/cc/run，所以生成 agent 时编译的是
#       /root/cc/run/extenders/linux_agent/src_agent/agent/*.c
#   而不是 /root/cc/AdaptixServer/extenders/linux_agent/src_agent/agent/*.c。
#   改了源码树不同步 run/ == 没改（本轮 ebpf_load 的 attach 修复就这样白跑了一轮）。
#   BOF 同理：matrix_resolver.go 从 <moduleDir>/bofs 取 .o。
#
# 用法: tools/deploy.sh            # 同步 agent 源码 + 重编全部 BOF + 复制到 run/
#       tools/deploy.sh --check    # 只对拍，不写（退出码 1 = 有漂移）
set -u

SRC=/root/cc/AdaptixServer/extenders/linux_agent
RUN=/root/cc/run/extenders/linux_agent
AGENT_SRC="$SRC/src_agent/agent"
AGENT_RUN="$RUN/src_agent/agent"
BOF_SRC="$SRC/src_agent/bofs"
BOF_RUN="$RUN/bofs"
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

drift=0
say() { printf '%-58s %s\n' "$1" "$2"; }

# ── 1. agent 源码（*.c/*.h）──────────────────────────────────────────────
for f in "$AGENT_SRC"/*.c "$AGENT_SRC"/*.h; do
    b=$(basename "$f")
    if ! cmp -s "$f" "$AGENT_RUN/$b"; then
        say "DRIFT agent/$b" "$(stat -c%s "$f")B != $(stat -c%s "$AGENT_RUN/$b" 2>/dev/null || echo missing)"
        drift=1
        [ $CHECK -eq 0 ] && cp -f "$f" "$AGENT_RUN/$b"
    fi
done

# ── 1b. mbedtls（JA4 书写器所在 —— 第四条构建轴，ISS-092）─────────
# ★ pl_main.go:486 NativeMbedtlsSrcDir="src_agent/mbedtls/library" 与 :499
#   -I src_agent/mbedtls/include -I src_agent/mbedtls
#   -DMBEDTLS_CONFIG_FILE='<mbedtls_config_agent.h>' 全是**相对路径**
#   ⇒ 运行期编的是 $RUN/src_agent/mbedtls/**。
#   它既是编译输入、又不在前三条轴（agent/BOF/BPF）里
#   ⇒ 改了 ssl_client.c 的 MBEDTLS_AX_JA4 块而 --check **假绿**、
#   运行态继续编旧 ClientHello（**静默退化**，最难发现的一类）。
# 判据 = `diff -rq` **逐文件比字节**，不是比 mtime（理由同 2b）。
MBED_SRC="$SRC/src_agent/mbedtls"
MBED_RUN="$RUN/src_agent/mbedtls"
if [ ! -d "$MBED_RUN" ]; then
    say "MISSING src_agent/mbedtls" "run/ 下不存在（生成 agent 会失败）"
    drift=1
    [ $CHECK -eq 0 ] && cp -a "$MBED_SRC" "$MBED_RUN"
elif ! diff -rq "$MBED_SRC" "$MBED_RUN" >/dev/null 2>&1; then
    say "DRIFT src_agent/mbedtls" "$(diff -rq "$MBED_SRC" "$MBED_RUN" 2>/dev/null | wc -l) 个文件差异"
    drift=1
    if [ $CHECK -eq 0 ]; then rm -rf "$MBED_RUN"; cp -a "$MBED_SRC" "$MBED_RUN"; fi
fi

# ── 2. BOF：重编全部，逐个对拍 .text 大小 ────────────────────────────────
WORK=$(mktemp -d /tmp/deploy_bof.XXXXXX)
for f in "$BOF_SRC"/*.c; do
    n=$(basename "$f" .c)
    if ! gcc -c -o "$WORK/$n.o" "$f" -include bof_api.h -Os -fPIC \
             -fno-stack-protector -I "$AGENT_SRC" 2>"$WORK/$n.err"; then
        say "FAIL compile $n.c" "$(head -3 "$WORK/$n.err" | tr '\n' ' ')"
        drift=1
        continue
    fi
    if ! cmp -s "$WORK/$n.o" "$BOF_RUN/$n.o"; then
        ts=$(readelf -SW "$WORK/$n.o" | awk '/\.text/{print $7; exit}')
        ts2=$(readelf -SW "$BOF_RUN/$n.o" 2>/dev/null | awk '/\.text/{print $7; exit}')
        say "DRIFT bofs/$n.o" ".text $ts != ${ts2:-missing}"
        drift=1
        [ $CHECK -eq 0 ] && cp -f "$WORK/$n.o" "$BOF_RUN/$n.o"
    fi
done
rm -rf "$WORK"

# ── 2b. BPF 产物对拍（ISS-082）───────────────────────────────────────────
# 为什么必须在这里：`bpf/out/*.insns.bin` 才是**运行期真正加载**的东西
# （ebpf_load.c 读它、patch map fd、attach），而它不在上面两步的覆盖范围内。
#
# 实测代价：`redact.c` 的 ISS-077 修复落盘后没重编，用 13:46 的旧 insns.bin
# 跑 p24 ⇒ T1/T2/T3 全绿、**T9 静默红**，而静默红极易被读成"类A 不生效、
# 回去改代码"，真相只是"没重编"。这是 ISS-005（agent 源码）/ ISS-081（BOF）
# 那条教训在**第三条构建轴**上的复发。
#
# 判据 = 重编到临时目录后**比字节**，不是比 mtime（理由见 build_bpf.sh）。
# 顺带走一遍 build_bpf.sh 自带的 verifier 门禁 —— clang PASS 只证明"能编译"，
# **不证明"能加载"**（redact 历史上就带着 invalid variable-offset 过了编译门，
# PROG_LOAD 才报 err=13）。
BPF_TMP=$(mktemp -d /tmp/deploy_bpf.XXXXXX)
for m in hide bpf_filter knock tc_rewrite redact; do
    [ -f "$SRC/src_agent/bpf/$m.c" ] || continue
    if ! AX_BPF_OUT="$BPF_TMP" bash "$SRC/tools/build_bpf.sh" "$m" >"$BPF_TMP/$m.log" 2>&1; then
        say "FAIL bpf/$m" "$(grep -E '\[FAIL\]' "$BPF_TMP/$m.log" | head -2 | tr '\n' ' ')"
        drift=1
        continue
    fi
    for art in insns.bin mapreloc.txt; do
        if ! cmp -s "$BPF_TMP/$m.$art" "$SRC/src_agent/bpf/out/$m.$art"; then
            say "DRIFT bpf/out/$m.$art" \
                "$(stat -c%s "$BPF_TMP/$m.$art")B != $(stat -c%s "$SRC/src_agent/bpf/out/$m.$art" 2>/dev/null || echo missing)"
            drift=1
            [ $CHECK -eq 0 ] && cp -f "$BPF_TMP/$m.$art" "$SRC/src_agent/bpf/out/$m.$art"
        fi
    done
done
rm -rf "$BPF_TMP"

# ── 3. dist/（make 的产物目录，保持与源码一致）──────────────────────────
if [ $CHECK -eq 0 ] && [ -d "$SRC/dist/src_agent/agent" ]; then
    cp -f "$AGENT_SRC"/*.c "$AGENT_SRC"/*.h "$SRC/dist/src_agent/agent/"
fi

if [ $drift -eq 1 ]; then
    [ $CHECK -eq 0 ] && echo "[deploy] 已同步到 $RUN" || echo "[deploy] 检测到漂移（--check 未写入）"
    exit 1
fi
echo "[deploy] 无漂移：agent 源码 + mbedtls($(ls "$MBED_SRC"/library/*.c | wc -l) 个 .c) + $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF + $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"
