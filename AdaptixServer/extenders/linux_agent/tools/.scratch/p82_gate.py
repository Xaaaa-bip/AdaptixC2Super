# -*- coding: utf-8 -*-
"""ISS-082：把 BPF 产物纳入漂移门禁。

为什么是"门禁"而不是"记得重编"：ISS-081 归档时写的
「靠人记得的步骤等于没有门禁」只补在了**它当时被发现的那条轴**上
（agent 源码 + BOF），`bpf/out/*.insns.bin` 那条轴**一直是裸的**。
代价已经实测过一次：`redact.c` 的 ISS-077 修复落盘后没重编，
用旧 insns.bin 跑 p24 ⇒ T1/T2/T3 全绿、**T9 静默红**，
而"静默红"最容易被读成"功能没生效，回去改代码"。

判据选**重编后比字节**而不是比 mtime：
  · mtime 会被 cp / rsync / 解包改，比它等于把"文件被碰过"当成"内容变了"；
  · 而"能不能编出同一份 insns.bin"才是**运行期加载的是不是这份源码**的定义。

两处改动：build_bpf.sh 让输出目录可覆盖（单一真源，不在 deploy.sh 里重抄
一遍 clang 命令行 —— 那会是下一条会漂移的轴），deploy.sh 加步骤 2b。
每处替换断言唯一命中，否则不写盘。
"""
import io, sys

R = []
FILES = {}


def patch(path, tag, old, new):
    s = FILES.setdefault(path, io.open(path, encoding="utf-8").read())
    n = s.count(old)
    R.append((tag, n))
    if n == 1:
        FILES[path] = s.replace(old, new)


SRC = "/root/cc/AdaptixServer/extenders/linux_agent"

# ══ build_bpf.sh：输出目录可覆盖 ═══════════════════════════════════════════
patch(SRC + "/tools/build_bpf.sh", "bpf-out", '''OUT="$BPFDIR/out"''',
      '''# ★ ISS-082：允许调用方把产物写到别处（deploy.sh --check 要编到临时目录
#   再与 out/ 对拍）。不这么做的话，deploy.sh 就得把下面整条 clang 命令行
#   抄一遍 —— 那就是**下一条会漂移的轴**，正是本文件要消灭的东西。
OUT="${AX_BPF_OUT:-$BPFDIR/out}"''')

# ══ deploy.sh：步骤 2b ════════════════════════════════════════════════════
patch(SRC + "/tools/deploy.sh", "step2b", '''# ── 3. dist/（make 的产物目录，保持与源码一致）──────────────────────────''',
      '''# ── 2b. BPF 产物对拍（ISS-082）───────────────────────────────────────────
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
        say "FAIL bpf/$m" "$(grep -E '\\[FAIL\\]' "$BPF_TMP/$m.log" | head -2 | tr '\\n' ' ')"
        drift=1
        continue
    fi
    for art in insns.bin mapreloc.txt; do
        if ! cmp -s "$BPF_TMP/$m.$art" "$SRC/src_agent/bpf/out/$m.$art"; then
            say "DRIFT bpf/out/$m.$art" \\
                "$(stat -c%s "$BPF_TMP/$m.$art")B != $(stat -c%s "$SRC/src_agent/bpf/out/$m.$art" 2>/dev/null || echo missing)"
            drift=1
            [ $CHECK -eq 0 ] && cp -f "$BPF_TMP/$m.$art" "$SRC/src_agent/bpf/out/$m.$art"
        fi
    done
done
rm -rf "$BPF_TMP"

# ── 3. dist/（make 的产物目录，保持与源码一致）──────────────────────────''')

# ══ deploy.sh：收尾那行的措辞要把 BPF 也算进去 ═══════════════════════════
patch(SRC + "/tools/deploy.sh", "summary", '''echo "[deploy] 无漂移：agent 源码 + $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF 与源码一致"''',
      '''echo "[deploy] 无漂移：agent 源码 + $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF + $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"''')

bad = [t for t, n in R if n != 1]
print("命中:", R)
if bad:
    print("ABORT 未命中/不唯一:", bad)
    sys.exit(1)
for p, s in FILES.items():
    io.open(p, "w", encoding="utf-8").write(s)
print("OK 已写入:", sorted(FILES))
