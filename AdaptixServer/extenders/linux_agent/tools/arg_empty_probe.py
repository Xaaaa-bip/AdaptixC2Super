#!/usr/bin/env python3
"""arg_empty_probe.py — 「参数为空」的两种形状，**各自**在一个全新 agent 上试一次。

为什么必须一形状一 agent（ISS-070）：
  缺 `param_data` 键曾让 agent **段错误**（内核日志：`segfault at <stack> ... in .m<hex>
  (deleted)`，故障指令是 memcpy 的字节拷贝循环）。若在同一个 agent 上连试两种形状，
  第二种的沉默与「被第一种连累」在观测上**完全同形** —— p20 的 T5b 就是这么被误判成
  "两跳回执抖动"的。所以：**一个新 agent 只试一种形状**；且试完**必须验活**
  （再打一发有回执的 BOF），否则"没输出"与"已死"分不开。

判据（四种结局必须能分开）：
  有回执 + 形状对 + 存活 → PASS
  有回执 + 形状错          → 服务器打包分支变了（不是崩溃）
  无回执 + 存活            → 通道问题（不是崩溃，另有原因）
  无回执 + 不活            → **命令打死了 agent**（ISS-070 的形状）

★ 判"有没有回执"只能看 **head4 是不是 None**。不能拿"blob 长度 > 0"当代理 ——
  两种形状的正确结果**本来就不同**（`empty`→4 字节 count=0；`omit`→**0 字节**），
  `blob > 0` 会把 `omit` 的正常结果判成失败（ISS-068 的同型错误，本工具初版就犯了）。

用法: python3 tools/arg_empty_probe.py [--listener TestHTTP]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e      # noqa: E402
import p20_arg_channel_verify as p20   # noqa: E402  复用 run_probe（含两跳回执等待）

# 形状 → 期望的 args blob 字节数。由服务器打包分支决定（pl_main.go:1611-1638）：
#   空串 ""  → base64 解出 0 字节 → 走明文回退 → 4 字节 count=0
#   缺键     → `if ok` 不成立 → params 保持 nil → 0 字节
EXPECT = {"empty": 4, "omit": 0}


def probe(aid, **kw):
    """返回 (head4, blob 长度)；**没采到回执** → (None, None)（下发异常也归一成它）。"""
    try:
        h, bl, _ = p20.run_probe(aid, **kw)[0]
        return h, bl
    except Exception:
        return None, None


def once(shape, listener):
    """在一个全新 agent 上：先验基线通道 → 试目标形状 → 验活。"""
    pid = None
    try:
        pid = e2e.launch(e2e.gen_agent(listener, "aep"))
        aid = e2e.wait_registered(listener, pid, timeout=90)
        base_h, base_bl = probe(aid, param_data="PING", use_b64=False, timeout=45, retries=0)
        if base_h is None:
            return (None, None), False, "基线通道就不通 → 本轮无效（不是本形状的结论）"
        if shape == "omit":
            got = probe(aid, param_data="", use_b64=False, omit=True, timeout=45, retries=0)
        else:
            got = probe(aid, param_data="", use_b64=False, timeout=45, retries=0)
        alive = probe(aid, param_data="ALIVE", use_b64=False, timeout=45, retries=0)[0] is not None
        return got, alive, "基线 blob=%s" % base_bl
    except Exception as e:
        return (None, None), False, "异常: %r" % (e,)
    finally:
        if pid:
            try:
                e2e.kill(pid)
            except Exception:
                pass


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"
    rc = 0
    for shape, desc in (("empty", '空串 "" → 服务器明文回退 → 4 字节 count=0 blob'),
                        ("omit", "缺 param_data 键 → Args=nil → 0 字节")):
        (h, bl), alive, note = once(shape, listener)
        want = EXPECT[shape]
        ok = h is not None and bl == want and alive
        if ok:
            verdict = "安全（回执 blob=%s，与预期一致）" % want
        elif not alive:
            verdict = "★ 打死了 agent"
        elif h is None:
            verdict = "无回执但存活 → 通道问题，不是崩溃"
        else:
            verdict = "有回执但形状不对（收到 blob=%s，预期 %s）" % (bl, want)
        print("[%s] %-5s %s" % ("PASS" if ok else "FAIL", shape, desc))
        print("        回执 blob=%s head4=%s | 试后存活=%s | 判定=%s | %s"
              % (bl, h, alive, verdict, note))
        if not ok:
            rc = 1
    print("RESULT: %s" % ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
