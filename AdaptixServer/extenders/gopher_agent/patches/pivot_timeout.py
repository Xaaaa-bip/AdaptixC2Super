#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 §4 步骤 5：pivot 拨号超时 200ms → 具名常量 3s。

    python3 patches/pivot_timeout.py check|apply|verify|revert

★ **这是一处有意的行为变更**（本阶段其余各步都是零行为变更），理由是**原值会让 UI 说谎**：
  `jobTunnel` 用 `net.DialTimeout(..., 200*time.Millisecond)` 拨 **pivot 目标**。真实网络里
  对端回 `ECONNREFUSED` 的 RTT 常常 >200ms ⇒ 本地先超时 ⇒ `reason=4`(timeout)，
  **而不是** `5`(ECONNREFUSED) ⇒ operator 在 UI 上看到的原因码是错的。
  （`reason` 码见 tasks.go:1278-1289，经 `utils.TermPack.Reason` 上报。）

★ 为什么是**常量**而不是 profile 字段：可配置要多改两处（agent Profile + teamserver 侧
  `pl_main.go` 的 case 分支），而收益只是"能调" —— 这一版的目标是**把原因码修对**。
  真需要按 profile 调时，再把常量换成字段（那时 `profile` 已在 `jobTunnel` 作用域内可得）。

★ **风险复核（已做）**：`tools/` 下**没有任何门禁断言 `reason` 码**（只有 `_fix_p16.py`
  提到过这个词），也没有门禁走 tunnel 路径 ⇒ 改动不波及现有 23 项。

★ 锚点是**纯文本**（不是行号）：本补丁与 `seam_transport.py` 会落在同一个文件上，
  行号会互相错位；文本锚点让**本补丁**对施加顺序不敏感。

★★ 但**顺序不是对称的 —— 必须 `seam_transport.py` 先、本补丁后**（这一条曾被上句话掩盖，已纠正）：
  本补丁在 `jobTunnel`（原 L1275）**之前**插入约 10 行，而 seam 的 `OPS` 用的是**行号**锚点，
  其中站点 4（L1293-1311）与站点 5（L1438-1456）都在插入点**之下** ⇒ 若本补丁先跑，
  这两个站点的行号会被顶偏约 10 行，**seam 会打到错的位置或直接拒绝**。
  ⇒ `apply` 里加了一条硬守卫：`transport.go` 不存在（= seam 没跑）就**拒绝落盘**。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src_gopher", "tasks.go")
SEAM_MARK = os.path.join(os.path.dirname(HERE), "src_gopher", "transport.go")
BAK = ".bak-pivot"

ANCHOR = "func jobTunnel(paramsData []byte) {\n"

CONST_DECL = """// pivotDialTimeout 是 pivot（隧道）拨向**目标**的超时。
//
// ★ 改造前这里是硬编码的 `200 * time.Millisecond`（Phase5 §4 步骤 5）。那个值
// 短到会让下面的原因码**说谎**：对端回 ECONNREFUSED 的 RTT 常常 >200ms ⇒ 本地先超时
// ⇒ 上报 reason=4(timeout) 而不是 5(ECONNREFUSED)。改成 3s 后，原因码才反映真实原因。
const pivotDialTimeout = 3 * time.Second

"""

OLD_DIAL = "net.DialTimeout(params.Proto, params.Address, 200*time.Millisecond)"
NEW_DIAL = "net.DialTimeout(params.Proto, params.Address, pivotDialTimeout)"


def state(t):
    has_const = "const pivotDialTimeout" in t
    has_old = OLD_DIAL in t
    has_new = NEW_DIAL in t
    if has_old and not has_const:
        return "old"
    if has_new and has_const:
        return "done"
    return "???"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    if not os.path.exists(SRC):
        print("[BAD ] 找不到 %s" % SRC)
        return 2
    t = open(SRC, encoding="utf-8").read()
    s = state(t)
    print("[%-4s] %s" % (s, os.path.relpath(SRC, os.path.dirname(HERE))))

    if mode == "check":
        if s == "???":
            print("       锚点计数: OLD_DIAL=%d ANCHOR=%d CONST=%d"
                  % (t.count(OLD_DIAL), t.count(ANCHOR), t.count("const pivotDialTimeout")))
        print("\ncheck: %s" % ("锚点匹配" if s != "???" else "锚点不匹配"))
        return 0 if s != "???" else 1

    if mode == "apply":
        if s == "???":                                   # 不过就一个字节都不写
            print("[BAD ] 锚点不匹配 —— 拒绝落盘")
            return 1
        if s == "done":
            print("[done] 已是目标态")
            return verify()
        # ★★ 顺序硬守卫：本补丁必须在 seam_transport.py **之后**跑（见文件头 ★★）。
        #    在目标态下 transport.go 由 seam 创建；它不在 ⇒ seam 没跑 ⇒ 拒绝。
        if s == "old" and not os.path.exists(SEAM_MARK):
            print("[BAD ] 缺 src_gopher/transport.go ⇒ seam_transport.py 还没跑。")
            print("       ★ 顺序要求：seam_transport → profile_fields → pivot_timeout。")
            print("       先跑此补丁会在 jobTunnel 前插入约 10 行，把 seam 的行号锚点（站点 4/5）顶偏。")
            return 1
        if t.count(OLD_DIAL) != 1 or t.count(ANCHOR) != 1:
            print("[BAD ] 锚点不唯一（dial=%d anchor=%d）—— 拒绝落盘"
                  % (t.count(OLD_DIAL), t.count(ANCHOR)))
            return 1
        shutil.copy2(SRC, SRC + BAK)
        t = t.replace(ANCHOR, CONST_DECL + ANCHOR, 1).replace(OLD_DIAL, NEW_DIAL, 1)
        open(SRC, "w", encoding="utf-8").write(t)
        print("[ok  ] 已改（备份 tasks.go" + BAK + "）")
        return verify()

    if mode == "revert":
        if not os.path.exists(SRC + BAK):
            print("[BAD ] 缺 tasks.go%s —— 拒绝 revert" % BAK)
            return 1
        shutil.move(SRC + BAK, SRC)
        print("[ok  ] 已还原")
        return 0

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


def verify():
    t = open(SRC, encoding="utf-8").read()
    checks = [
        ("常量已声明", t.count("const pivotDialTimeout = 3 * time.Second") == 1),
        ("拨号已改用常量", t.count(NEW_DIAL) == 1),
        ("旧字面量已消失", t.count("200*time.Millisecond") == 0),
        ("常量在 jobTunnel 之前", t.find("const pivotDialTimeout") < t.find(ANCHOR)
         if "const pivotDialTimeout" in t else False),
    ]
    for name, ok in checks:
        print("[%s] %s" % ("ok  " if ok else "BAD ", name))
    bad = sum(1 for _, ok in checks if not ok)
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
