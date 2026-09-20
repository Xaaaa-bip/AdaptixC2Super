#!/bin/sh
# ISS-087 自检：`build_bpf.sh` 的"要么全对、要么不动"不变量。
#
#   A 半（清理）: out/ 里**本来没有** zztest 产物 ⇒ 失败后它们必须**消失**；
#   B 半（还原）: out/ 里**已经有** zztest 产物 ⇒ 失败后必须**逐字节回到原样**。
#
# ★ B 半才是 ISS-087 的真实场景（旧的好产物被本次的坏产物覆盖），而它**最容易
#   被假绿糊过去**：如果失败发生在写盘**之前**，"md5 没变"就等于"根本没被写，
#   当然没变"。所以每条断言都配了一个"判别力前提"检查 —— 必须先在日志里看见
#   `[PASS] insns.bin zztest`（产物确实落盘）**且** `[FAIL] verifier zztest`
#   （失败点在写盘之后），那条 md5 断言才有意义。
#
# ★ 自检模块的形状很要紧：第一版用"无界循环"，实测量到 verifier 要烧 **5 分钟
#   以上**才吐 `Processed 1000001 insn`（1965MB 的小机）。改用**实参类型错**后
#   隔离预检是 **14ms** / `processed 2 insns (limit 1000000)` /
#   `R1 type=ctx expected=map_ptr` —— 同一个 limit 的两端，确定、快、可重复。
#
# ★ 跑之前确认没有别的 build/deploy 在跑（它会往真源 out/ 里种哨兵）。
set -u
cd /root/cc/AdaptixServer/extenders/linux_agent || exit 1
B=src_agent/bpf/out
S=tools/.scratch

# 四类产物全在内 —— `.o` 也要，否则测不出 ISS-087 的那个补漏
snap() { md5sum $B/*.insns.bin $B/*.mapreloc.txt $B/*.attach.txt $B/*.o 2>/dev/null | sort -k2; }
chk()  { if [ "$2" = "$3" ]; then echo "  [PASS] $1"; else echo "  [FAIL] $1  ($2 != $3)"; fi; }
run()  { bash tools/build_bpf.sh zztest 2>&1 | tee "$1" | grep -E 'PASS|FAIL|BUILD_BPF|还原|expected|invalid' | head -12; }

# 前置：还有别的 build 在跑就别动
if pgrep -f "[b]uild_bpf.sh" >/dev/null 2>&1; then
    echo "ABORT：还有 build_bpf.sh 在跑（它正在用 out/，不能种哨兵）"; exit 1
fi

cp -f $S/zztest_fast.c src_agent/bpf/zztest.c || exit 1

echo "════════ B 半：还原（out/ 里**已有**旧产物）════════"
for ext in insns.bin mapreloc.txt attach.txt o; do
    printf 'ZZSENTINEL-%s\n' "$ext" > "$B/zztest.$ext"
done
snap > $S/p87b_before.txt
echo "  哨兵（失败前）: $(ls -l $B/zztest.* | awk '{printf "%s=%sB ", $9, $5}')"
echo "── 跑一次注定失败的构建 ──"
run $S/p87b_build.log
snap > $S/p87b_after.txt
echo "  哨兵（失败后）: $(ls -l $B/zztest.* 2>/dev/null | awk '{printf "%s=%sB ", $9, $5}')"
echo "── 判别力前提（没有这两条，下面的 md5 断言无意义）──"
chk "产物**确实落盘**了"            "$(grep -c 'PASS. insns.bin zztest' $S/p87b_build.log)" 1
chk "失败点**在写盘之后**(verifier)" "$(grep -c 'FAIL. verifier zztest'  $S/p87b_build.log)" 1
chk "还原分支**确实进了**"           "$(grep -c '有检查未通过'        $S/p87b_build.log)" 1
echo "── 断言 ──"
if diff -q $S/p87b_before.txt $S/p87b_after.txt >/dev/null; then
    echo "  [PASS] ★★ 双向还原：失败后 out/ 全部产物（含 .o）与开跑前**逐字节相同**"
else
    echo "  [FAIL] ★★ 双向还原失败："; diff $S/p87b_before.txt $S/p87b_after.txt
fi
chk "快照目录无残骸" "$(ls -d $B/.snap.* 2>/dev/null | wc -l)" 0

echo
echo "════════ A 半：清理（out/ 里**没有**旧产物）════════"
rm -f $B/zztest.*
snap > $S/p87a_before.txt
echo "  开跑前 zztest 产物数: $(ls $B/zztest.* 2>/dev/null | wc -l)"
echo "── 跑一次注定失败的构建 ──"
run $S/p87a_build.log
snap > $S/p87a_after.txt
echo "── 断言 ──"
chk "★ 新造出来的坏产物被清掉（四类残骸归零）" "$(ls $B/zztest.* 2>/dev/null | wc -l)" 0
if diff -q $S/p87a_before.txt $S/p87a_after.txt >/dev/null; then
    echo "  [PASS] 5 个真模块的产物**一个字都没被牵连**"
else
    echo "  [FAIL] 真模块产物被牵连："; diff $S/p87a_before.txt $S/p87a_after.txt
fi
chk "快照目录无残骸" "$(ls -d $B/.snap.* 2>/dev/null | wc -l)" 0

echo
echo "════════ 收尾：把自检模块从产品树里摘掉 ════════"
rm -f src_agent/bpf/zztest.c
echo "src_agent/bpf/zztest.c 还在吗: $([ -f src_agent/bpf/zztest.c ] && echo 在 || echo 已删)"
echo "out/ 里 zztest 残骸: $(ls $B/zztest.* 2>/dev/null | wc -l) 个（应为 0）"
echo "out/ 现有产物: $(ls $B | tr '\n' ' ')"
