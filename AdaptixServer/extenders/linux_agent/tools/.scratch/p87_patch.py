# -*- coding: utf-8 -*-
"""ISS-087：`build_bpf.sh` 的 verifier 门禁**跑在写盘之后** ⇒ 失败的产物留在运行期真源。

现场（2026-09-16，P9-9.5-① 实测）：
    [PASS] insns.bin redact (16696 bytes = 2087 insns, ...)   ← 产物已落 out/
    [FAIL] verifier redact (type=tracepoint)
    BPF program is too large. Processed 1000001 insn
⇒ 一个**加载不了**的 redact 在 `out/` 里躺了约 10 分钟。而 `out/*.insns.bin`
  **就是运行期真正加载的东西**（deploy.sh:59 注释原文、ebpf_load.c 读它）
  ⇒ 期间任何台面都会假红，而症状（PROG_LOAD err=13 / "redact 加载失败"）与
  **"功能真的坏了"逐字节同形**。

★ 与 ISS-077 是同一族的两半，方向相反：
    ISS-077 = 源码新、产物旧（落盘后没重编）
    ISS-087 = 源码坏、产物**已被换成坏的**（门禁在写盘之后）

修法 = 开跑前快照 `out/` 的三类产物，任一检查失败就**整体还原**（全有或全无：
out/ 要么与源码一致，要么与改动前一致 —— 半新半旧比全旧更坏）。
还原时双向：快照里**有**的覆盖回去，快照里**没有**的（本次新造的坏产物）删掉。
`AX_BPF_OUT` 被覆盖时跳过（deploy.sh 用它编到临时目录再对拍，那里本就不落真源）。
"""
import io
import sys

F = "/root/cc/AdaptixServer/extenders/linux_agent/tools/build_bpf.sh"
src = io.open(F, encoding="utf-8").read()

OLD = '''FAIL=0
if [ "$MODULE" = "all" ]; then
    for m in hide bpf_filter knock tc_rewrite redact; do
        build_one "$m" || FAIL=1
    done
else
    build_one "$MODULE" || FAIL=1
fi

[ "$FAIL" -eq 0 ] && echo "BUILD_BPF: ALL PASS" || echo "BUILD_BPF: FAIL"
exit $FAIL'''

NEW = '''# ── ★★ ISS-087：产物必须"要么全对、要么不动" ────────────────────────────────
# 上面 build_one 把 insns.bin / mapreloc.txt / attach.txt **直写** $OUT，而 out/
# 就是**运行期真正加载**的那个目录（ebpf_load.c 读 insns.bin、按 mapreloc patch
# map fd；deploy.sh:59 有同一句），可 verifier 门禁跑在**写盘之后**。于是"编译
# 过了、verifier 拒绝"的产物会**静默留在真源位置**：旧的好产物已被覆盖，而症状
# （PROG_LOAD err=13 / 台面报 "xx 加载失败"）与"功能真的坏了"**逐字节同形**。
# 实测代价：一次 verifier FAIL 让加载不了的 redact 在 out/ 里躺了约 10 分钟。
# 修法：开跑前快照、失败即整体还原。还原要**双向** —— 快照里有的覆盖回去，
# 快照里没有的（本次新造的坏产物）删掉，否则"新模块失败"仍会留残骸。
# AX_BPF_OUT 覆盖时跳过：deploy.sh 用它把产物编到临时目录再与 out/ 对拍字节，
# 那里本来就不落真源。
SNAP=""
if [ -z "${AX_BPF_OUT:-}" ]; then
    SNAP="$(mktemp -d "$OUT/.snap.XXXXXX" 2>/dev/null)" || SNAP=""
    if [ -n "$SNAP" ]; then
        cp -p "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \\
            "$SNAP/" 2>/dev/null || true
    fi
fi

FAIL=0
if [ "$MODULE" = "all" ]; then
    for m in hide bpf_filter knock tc_rewrite redact; do
        build_one "$m" || FAIL=1
    done
else
    build_one "$MODULE" || FAIL=1
fi

if [ -n "$SNAP" ]; then
    if [ "$FAIL" -eq 0 ]; then
        rm -rf "$SNAP"
    else
        echo "  → 有检查未通过：把 out/ 还原成**本次改动前**的产物（ISS-087）"
        for f in "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt; do
            [ -f "$f" ] || continue
            b="$SNAP/$(basename "$f")"
            if [ -f "$b" ]; then
                cp -p "$b" "$f" || true
            else
                rm -f "$f"            # 快照里没有 = 本次新造出来的 ⇒ 一并清掉
            fi
        done
        rm -rf "$SNAP"
    fi
fi

[ "$FAIL" -eq 0 ] && echo "BUILD_BPF: ALL PASS" || echo "BUILD_BPF: FAIL"
exit $FAIL'''

n = src.count(OLD)
print("锚点命中: %d" % n)
if n != 1:
    sys.exit("ABORT（未命中或不唯一）")
src = src.replace(OLD, NEW, 1)
io.open(F, "w", encoding="utf-8").write(src)
print("OK 已写入 %s" % F)
