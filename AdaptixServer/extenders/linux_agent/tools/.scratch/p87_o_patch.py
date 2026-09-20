# -*- coding: utf-8 -*-
"""ISS-087 补漏：`.o` 不在快照/双向还原范围内。

★ 为什么 `.o` 算产物（三条都是 could-verify 的硬事实，不是印象）：
  ① `build_bpf.sh` 头注第 8-12 行把 `<module>.o` 与 insns.bin / mapreloc.txt /
     attach.txt **并列**列在 "Outputs (src_agent/bpf/out/)" 之下；
  ② `build_one:144` 的 `obj="$OUT/$mod.o"` 确实**直写**那里；
  ③ 实测 out/ 里 5 个 `.o` 合计约 2.6MB（hide.o 852KB / redact.o 914KB /
     tc_rewrite.o 859KB / knock.o 17KB / bpf_filter.o 10KB）。

★ 但它**不是运行期产物** —— 运行期只读 insns.bin（`ebpf_load.c` 读它、按
  mapreloc patch map fd）。所以漏掉 `.o` 的害处不是"功能坏"，而是**另一条**：
  "失败后 out/ 与开跑前逐字节相同"这条不变量被破坏；且下次谁看见一个 `.o`，
  无法判断它出自"过门禁的那次"还是"被 verifier 拒的那次" —— 这与 ISS-087 本体
  （"编译过了、verifier 拒绝"的产物静默留在真源位置）是**同一类混淆，低一层**。

★ 本脚本的自我约束（上一次补丁的教训）：正则/替换若把"锚点之前的上下文行"写进
  NEW，而锚点从锚点行起 ⇒ 该行会重复。所以每处替换**断言唯一命中**，写完再把
  改动区的**纯文本**打印出来眼看一遍。
"""
import io
import sys

F = "/root/cc/AdaptixServer/extenders/linux_agent/tools/build_bpf.sh"

PATCHES = [
    (
        "注释：把 .o 的理由写进设计说明",
        '''# 快照里没有的（本次新造的坏产物）删掉，否则"新模块失败"仍会留残骸。
''',
        '''# 快照里没有的（本次新造的坏产物）删掉，否则"新模块失败"仍会留残骸。
# ★ `.o` 也在快照/还原范围内。它**不是运行期产物**（运行期只读 insns.bin，
#   见 ebpf_load.c），但头注 "Outputs" 一节把它与另三类并列、build_one:144 也
#   直写 $OUT ⇒ 留着它就破坏"失败后 out/ 与开跑前逐字节相同"这条不变量，且下次
#   看见一个 .o 无法判断它出自"过门禁的那次"还是"被拒的那次" —— 与 ISS-087
#   本体是同一类混淆，低一层。
''',
    ),
    (
        "快照：带上 *.o",
        '''        cp -p "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \\
            "$SNAP/" 2>/dev/null || true''',
        '''        cp -p "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \\
            "$OUT"/*.o "$SNAP/" 2>/dev/null || true''',
    ),
    (
        "还原：带上 *.o",
        '''        for f in "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt; do''',
        '''        for f in "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \\
                 "$OUT"/*.o; do''',
    ),
]


def main():
    src = io.open(F, encoding="utf-8").read()
    for tag, old, new in PATCHES:
        n = src.count(old)
        print("%-28s 命中: %d" % (tag, n))
        if n != 1:
            print("ABORT：未命中或不唯一，不写盘")
            return 1
        src = src.replace(old, new)
    io.open(F, "w", encoding="utf-8").write(src)
    print("OK 已写入")

    # ★ 眼看一遍改动区（纯文本，不做任何过滤）—— 上一次的重复行就是靠这一步抓到的
    print("\n=== 改动区原文（快照段）===")
    txt = io.open(F, encoding="utf-8").read().splitlines()
    for i, ln in enumerate(txt):
        if "SNAP=\"\"" in ln:
            for j in range(i, min(i + 14, len(txt))):
                print("%4d| %s" % (j + 1, txt[j]))
            break
    print("\n=== 改动区原文（还原段）===")
    for i, ln in enumerate(txt):
        if "有检查未通过" in ln:
            for j in range(i - 1, min(i + 13, len(txt))):
                print("%4d| %s" % (j + 1, txt[j]))
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
