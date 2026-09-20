# -*- coding: utf-8 -*-
"""p20 加 T6/T7（长度维度判据）+ run_regress.sh 加 deploy 漂移门禁 / tail -12。"""
import io, sys

R = []
FILES = {}


def patch(path, tag, old, new):
    s = FILES.setdefault(path, io.open(path, encoding="utf-8").read())
    n = s.count(old)
    R.append((tag, n))
    if n == 1:
        FILES[path] = s.replace(old, new)


P20 = "/root/cc/AdaptixServer/extenders/linux_agent/tools/p20_arg_channel_verify.py"
RR = "/root/cc/AdaptixServer/extenders/linux_agent/tools/run_regress.sh"

# ══ p20 : T6 / T7 ═════════════════════════════════════════════════════════
patch(P20, "T6T7", '''            "缺失: len=%s head4=%s | 空串: len=%s head4=%s | 验活 前=%s 后=%s"
            % (bl5a, h5a, bl5b, h5b, live_pre, live_post))''',
'''            "缺失: len=%s head4=%s | 空串: len=%s head4=%s | 验活 前=%s 后=%s"
            % (bl5a, h5a, bl5b, h5b, live_pre, live_post))
        life("T5-done", aid, pid)

        # ── T6 ★★ 一个**空字段**（count=1，唯一那个字段长度声明为 0）──
        #   这是全台面**唯一**能压到"**长度维度**"的形状，也是本脚本原先缺的一格：
        #   T5 的两种形状（键缺失 / 空串）里 `len` 都是 0 **且 `p == NULL`**（走 break），
        #   而这里 `parser->length == 4` ⇒ Extract 走到正常路径 ⇒ 返回
        #   **非 NULL 指针 + `*size == 0`** —— 于是 argprobe 的
        #   `n = (len > 0 && len < 79) ? len : 79` 取到 **n = 79**，
        #   从裸指针越界拷 79 字节。
        #   ★ 判据是**逐字**要求 tok[0] 为空串：修复前必然打出垃圾（或崩），
        #     修复后有且只有一种结果（`''`）。p23 只查"指针/NUL 维度"，
        #     把这种"指针非空、长度为零"判成 SAFE —— 判据必须覆盖**每个维度**。
        (h6, bl6, t6), _ = run_probe(aid, blob(b""), use_b64=True, timeout=45, retries=1)
        ok6 = (h6 == 1 and bl6 == 8 and t6[:1] == [""])
        rec("T6 ★★空字段(非NULL+len=0)：探针必须逐字打出空串",
            "PASS" if ok6 else "FAIL",
            "head4=%s blob=%s tok[0]=%r（应为 1 / 8 / ''）" % (h6, bl6, t6[:1]))
        life("T6-emptyfield", aid, pid)

        # ── T7 ★ 空字段**夹在中间**：证明"空"不终止迭代 ──
        #   这条锁住的是修 F1 时的一个**设计决定**：`length == 0` 仍然返回
        #   **非 NULL** 指针，而不是归一成 NULL。归一会让 `if (!p) break;` 在第二个
        #   字段就退出，第三个字段 'C' 永远取不到 —— 合法的"某个参数为空"
        #   就变成了**参数丢失**。没有这条判据，后人"顺手把 length==0 改成回 NULL"
        #   不会有任何台面变红（判据失效比判据变红更危险）。
        (h7, bl7, t7), _ = run_probe(aid, blob(b"A", b"", b"C"), use_b64=True,
                                     timeout=45, retries=1)
        ok7 = (h7 == 3 and t7[:3] == ["A", "", "C"])
        rec("T7 ★空字段在中间：后面的字段仍取得到（空 != 结束）",
            "PASS" if ok7 else "FAIL",
            "head4=%s toks=%r（应为 3 / ['A', '', 'C']）" % (h7, t7[:3]))
        life("T7-midempty", aid, pid)''')

# ══ run_regress.sh : 漂移门禁 ═════════════════════════════════════════════
patch(RR, "gate", '''rm -f /tmp/reg.log''',
'''# ── ★★ 前置门禁：源码树 vs 运行树的漂移（ISS-078 家族）──────────────────
# 为什么必须挡在跑批**之前**：pl_main.go 的 `NativeSrcDir` 是**相对路径**
# （`src_agent/agent`，相对服务器进程 cwd=/root/cc/run），所以服务端现编 agent
# 读的是 **run/ 下的源码副本**；BOF 更是直接取 run/bofs/*.o。
# 「改了源码树不 deploy == 没改」是 ISS-005 那条教训在**构建轴**上的同一个坑。
#
# 实测代价：17 项全部跑完之后才发现 run/ 里的 `bof_api.h` 与 3 个 BOF
# （evacuate / ld_preload_check / ld_preload_install）都还是旧的 ——
# **整批结论的适用对象都不对**，而且这是事后才发现的（`deploy.sh` 一跑就打印出来）。
# 靠人记得 deploy 是不可靠的；这条不变量只能由台面自己守。
rm -f /tmp/reg.log''')

patch(RR, "gate-body", '''for spec in \\''',
'''drift_out=$(./tools/deploy.sh --check 2>&1)
if [ $? -ne 0 ]; then
    {
        echo "FATAL: 源码树与 run/ 有漂移，拒绝跑批 —— 跑出来的结论不适用于任何东西。"
        echo "       先执行 ./tools/deploy.sh 同步，再重跑本批。"
        echo "$drift_out"
    } >> /tmp/reg.log
    exit 1
fi
echo "[batch] deploy --check 通过：agent 源码 + BOF 与源码树一致" >> /tmp/reg.log

for spec in \\''')

patch(RR, "tail12", '''    tail -6 "/tmp/reg_$t.out" >> /tmp/reg.log''',
'''    # ★ 6 → 12 行：p15 的 `RESULT:` 行曾被 `tail -6` 挤出 /tmp/reg.log
    #   （输出变长之后 6 行只够容纳前缀噪声），于是"汇总行没进日志"看起来像
    #   "脚本没有汇总行"。行数是判据的**载具**，载具太短 = 判据丢失。
    tail -12 "/tmp/reg_$t.out" >> /tmp/reg.log''')

patch(RR, "p20to", '''p19_g7_closure_verify:600 p20_arg_channel_verify:900 \\''',
'''p19_g7_closure_verify:600 p20_arg_channel_verify:1200 \\''')

bad = [t for t, n in R if n != 1]
print("命中:", R)
if bad:
    print("ABORT 未命中/不唯一:", bad)
    sys.exit(1)
for p, s in FILES.items():
    io.open(p, "w", encoding="utf-8").write(s)
print("OK 已写入:", sorted(FILES))
