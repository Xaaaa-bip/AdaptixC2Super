#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次7：`fresh()` 的观测量里混进了**它自己的输出** ⇒ `install` 恒红（假红）。

## 实测真事件（2026-09-17，用户授权运行态验证时第一次跑到）

    $ l2p_deploy.sh install
    [BAD ] .../gopher_agent/dist/agent_gopher.so 比这些源文件旧 ⇒ dist 不是当次构建的，拒绝安装：
           .../dist/src_gopher/{ax_dial_test.go, transport_libp2p.go, main.go, …}   ← 27 个
    RC=1

根因是 `gopher_agent/Makefile` 的**顺序**：

    ① go build -buildmode=plugin -o ./dist/agent_gopher.so .      ← .so mtime = T1
    ② (cd src_gopher && make)                                     ← 六平台冒烟
    ③ cp -r src_gopher ./dist/src_gopher                          ← T2 > T1（**cp 不保 mtime**）

⇒ `dist/src_gopher/**/*.go` 的 mtime **必然**晚于 `dist/agent_gopher.so`
⇒ 原判据 `find "$src" -name '*.go' -newer "$so"`（`$src` = 整棵 `$AGT`）**恒红**。

**实测三条对照（决定性）**：

| 度量 | 值 |
|---|---|
| `find $AGT -name '*.go' -newer $SO`（原判据） | **27** |
| 其中**在 `dist/` 之外**的 | **0** ← 全部是自己的输出 |
| `find $AGT -name '*.go' -not -path '*/dist/*' -newer $SO` | **0** |
| `find $LST -name '*.go' -newer $LST/dist/*.so` | **0** ← 解释 listener 侧为何通过（其 `dist/` 无 `.go`） |

## ★ 定性：**不是"门对树错"，是门的观测量里混进了它自己的输出**

批次6 是"门对树错"（树里有不该有的回滚点）；**本批次正相反**：
门要判的是"**这个 .so 是当次源码构建的**"，而它去扫的 `dist/src_gopher` 是**构建的产物**，
不是构建的输入。**红是假红**（本项目最忌的一类判据：红的理由必须是真理由）。
⇒ 唯一该修的是**门**，而且修法不是放松 —— 是**把自己的输出移出观测量**。

## ★★ 顺带修正一条纪律：这道门**从未被证明能绿**

`fresh()` 只在 `do_install` 里被调用：`build` 不调它、`verify` 不调它、`restart` 不调它。
而 `install` 在本项目历史上**从来没跑成功过**（批次5 那次被 auto 模式分类器拒绝，未绕过）。
⇒ 它此前只有"设计"没有"绿证"。**"门要能红也能绿才算门"** —— 本批次把它补上。

## 本脚本做两件事

**① `fresh()` 排除 `dist/`**：判定扫描加 `-not -path '*/dist/*'`。
   判定本身（`*.go` 里不得有比 .so 新的）**原样保留** —— 它要抓的是"改了 `src_gopher` 却忘了重建"，
   那个场景下 `src_gopher/**/*.go` 确实会新于 .so，判据照样红（本脚本的 verify 有**红侧**实证）。

**② 把假红真正想说的话换成一条内容判据**：`do_install` 里加
   `diff -rq "$AGT/src_gopher" "$AGT/dist/src_gopher"` —— **比字节，不比 mtime**。
   它比原来那条更强：原判据只能间接推断"大概没改过"，这条直接证明
   **交付给 operator 的源码副本与源码树逐文件逐字节一致**。
   （原来这条检查**不存在** —— 批次5 是手工 `diff -rq` 过一次，从没进判据。）

## 不做的（如实登记）

- **不给 Makefile 加 `cp -r -p`**：`dist/` 一旦移出观测量，`-p` 就不再是必要件；
  而它会把"交付副本的 mtime"也钉成和源码一样，反而掩盖别的 mtime 信号。登记为候选，不施加。
- 不动 `fresh()` 里那条 `[DIAG]` 检查（`cnt "$so" '[DIAG]' = 0`）—— 它是对的，且实测能红。

用法：
    l2p_freshfix.py check | lint | apply | revert | verify
    （verify 含**红/绿双向**，且全部**不碰运行态**：绿=当前树；红=在 `src_gopher` 放一个
      比 .so 新的探针 .go ⇒ 修好的判据必须照样报红；内容判据则用 `dist/src_gopher` 里的
      探针文件验红。两处探针都在 `finally` 里清除。）
"""
import os
import shutil
import subprocess
import sys

EXT = "/root/cc/AdaptixServer/extenders"
LST = os.path.join(EXT, "gopher_listener_tcp")
AGT = os.path.join(EXT, "gopher_agent")
BAKDIR = "/root/gp5probe/bak"
DEPLOY = os.path.join(LST, "patches", "l2p_deploy.sh")
AGTSO = os.path.join(AGT, "dist", "agent_gopher.so")
LSTSO = os.path.join(LST, "dist", "listener_gopher_tcp.so")
PROBE = "/root/gp5probe/freshfix_probe"


def ok(m):
    print("  [ok  ] %s" % m)


def bad(m):
    print("  [BAD ] %s" % m)


def info(m):
    print("  [info] %s" % m)


# ---------------------------------------------------------------------------
# 文本改动。★ 新文本**不得包含**旧文本（lint 强制；批次6 在这里栽过两次）
# ---------------------------------------------------------------------------
A1_OLD = '''[DIAG] 插桩（dist 不是干净源码构建的），拒绝安装"
  newer=$(find "$src" -name '*.go' -newer "$so" 2>/dev/null)
'''

A1_NEW = '''[DIAG] 插桩（dist 不是干净源码构建的），拒绝安装"
  # ★ 批次7（部署链解阻·续）：**必须排除 `dist/`** —— 它是本次构建**自己的输出**。
  #   实测真事件：`install` 报 `$AGT/dist/agent_gopher.so 比这些源文件旧`，列出 27 个文件
  #   **全部**在 `dist/src_gopher/` 下。根因是 `gopher_agent/Makefile` 的顺序：
  #     ① `go build -o ./dist/agent_gopher.so .`（T1）
  #     ② `(cd src_gopher && make)`（六平台冒烟）
  #     ③ `cp -r src_gopher ./dist/src_gopher`（T2 > T1，**cp 不保 mtime**）
  #   ⇒ `dist/src_gopher/**/*.go` 的 mtime **必然**晚于 .so ⇒ 含 dist 的原判据**恒红**。
  #   ★ 定性：**不是"门对树错"，是门的观测量里混进了它自己的输出**（红是**假红**）。
  #     本判据要抓的是"改了 `src_gopher` 却忘了重建" —— 那个场景下 `src_gopher/**/*.go`
  #     确实会新于 .so，**排除 dist 之后判据照样红**（patches/l2p_freshfix.py 的 verify 有红侧实证）。
  #   实测对照：含 dist ⇒ newer=27（且全部在 dist/ 内）；排除后 ⇒ newer=0。
  #   ★ 本判据**首次被行使**就是批次7 —— `build`/`verify`/`restart` 都不调 `fresh`，
  #     而 `install` 此前从未跑成功过 ⇒ 它此前只有"设计"没有"绿证"。
  newer=$(find "$src" -name '*.go' -not -path '*/dist/*' -newer "$so" 2>/dev/null)
'''

A2_OLD = '''  pkgsanity "$AGT/dist/src_gopher"
  ok "dist 两枚 .so 均为当次源码构建、无 DIAG 插桩"
'''

A2_NEW = '''  pkgsanity "$AGT/dist/src_gopher"
  # ★ 批次7：把 `fresh()` 那条假红**真正想说的话**换成一条内容判据 —— **比字节，不比 mtime**。
  #   它比原来那条更强：原来只能间接推断"大概没改过"，这条直接证明
  #   **交付给 operator 的源码副本与源码树逐文件逐字节一致**。
  #   （这条检查原来**不存在**：批次5 是手工 `diff -rq` 过一次，从未进判据。）
  diff -rq "$AGT/src_gopher" "$AGT/dist/src_gopher" >/dev/null 2>&1 \\
    || die "交付的源码副本 dist/src_gopher 与源码树 src_gopher 不一致 ⇒ 拒绝安装（operator 会拿到不同源的源码）"
  ok "dist/src_gopher 与 src_gopher 内容一致（逐文件逐字节）"
  ok "dist 两枚 .so 均为当次源码构建、无 DIAG 插桩"
'''

TEXTS = [(DEPLOY, A1_OLD, A1_NEW), (DEPLOY, A2_OLD, A2_NEW)]
EDBACK = {DEPLOY: os.path.join(BAKDIR, "l2p_deploy.sh.bak-freshfix")}
MOVES = []


def read(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def write(p, t):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(t)


def lint():
    errs = []
    for i, (path, old, new) in enumerate(TEXTS):
        if old in new:
            errs.append("TEXTS[%d] %s：**新文本包含旧文本** ⇒ 落盘后 旧=1 新=1，回读必判 ???"
                        % (i, os.path.basename(path)))
        if old == new:
            errs.append("TEXTS[%d] %s：新旧一样" % (i, os.path.basename(path)))
    return errs


def tstate(path, old, new):
    if not os.path.exists(path):
        return "???", "缺文件"
    t = read(path)
    no, nn = t.count(old), t.count(new)
    if no == 1 and nn == 0:
        return "old", None
    if no == 0 and nn == 1:
        return "done", None
    return "???", "锚点命中 旧=%d 新=%d（要求恰好一个为 1）" % (no, nn)


def items():
    out = []
    for path, old, new in TEXTS:
        s, w = tstate(path, old, new)
        out.append(("%s :: %s" % (os.path.basename(path), old.split("\n")[0][:46]),
                    "text", (path, old, new), s, w))
    return out


def report():
    rows = items()
    n = {}
    for label, _k, _p, s, w in rows:
        n[s] = n.get(s, 0) + 1
        print("  [%-4s] %s%s" % (s, label, ("  ← %s" % w) if w else ""))
    print("  ---- old=%d done=%d ???=%d ----" % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
    return rows, n


def do_apply():
    es = lint()
    if es:
        for e in es:
            bad(e)
        print("RESULT: FAIL（锚点自检不过，未碰任何文件）")
        return 1
    rows, n = report()
    if n.get("???", 0):
        bad("有 %d 项歧义 ⇒ 拒绝落盘" % n["???"])
        print("RESULT: FAIL")
        return 1
    if n.get("old", 0) == 0:
        ok("全部已是对目标态")
        print("RESULT: PASS")
        return 0
    for label, kind, pay, s, _w in rows:
        if s != "old":
            info("跳过（已 done）：%s" % label)
            continue
        path, old, new = pay
        t = read(path)
        if t.count(old) != 1:
            bad("%s 锚点在落盘前一刻命中 %d 次 ⇒ 中止" % (label, t.count(old)))
            print("RESULT: FAIL")
            return 1
        if not os.path.exists(EDBACK[path]):
            os.makedirs(BAKDIR, exist_ok=True)
            shutil.copy2(path, EDBACK[path])
            ok("被改文件回滚点 → %s" % EDBACK[path])
        write(path, t.replace(old, new, 1))
        ok("写入 %s（%s）" % (os.path.basename(path), label.split(":: ")[-1][:34]))
    rows, n = report()
    if n.get("done", 0) != len(rows):
        bad("落盘后回读不是目标态（old=%d done=%d ???=%d）"
            % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("落盘后回读 = 目标态（%d/%d）" % (n["done"], len(rows)))
    print("RESULT: PASS")
    return 0


def do_revert():
    rows, n = report()
    if n.get("???", 0):
        bad("有 %d 项歧义 ⇒ 拒绝回滚" % n["???"])
        print("RESULT: FAIL")
        return 1
    if n.get("done", 0) == 0:
        ok("全部已是原始态")
        print("RESULT: PASS")
        return 0
    plan = []
    for label, kind, pay, s, _w in rows:
        if s != "done":
            info("跳过（已 old）：%s" % label)
            continue
        path, old, new = pay
        t = read(path)
        if t.count(new) != 1:
            bad("%s 反向锚点在落盘前一刻命中 %d 次 ⇒ 中止" % (label, t.count(new)))
            print("RESULT: FAIL")
            return 1
        back = t.replace(new, old, 1)
        plan.append((path, back, label))
    # 逐项反向替换是**累积**的：同一个文件的两处必须串行作用在同一份文本上
    # （★ 批次21 的教训「原子 ≠ 串行」；此处逐项顺序作用，最后整体落盘）
    merged = {}
    for path, back, _l in plan:
        base = merged.get(path, read(path))
        # 用该文件当前（已回滚部分）的文本继续替换
        merged[path] = base if path not in merged else base
    # 重新按项串行计算（避免上面的歧义写法）：从当前文本出发，逐项做反向替换
    cur = {p: read(p) for p, _o, _n in TEXTS}
    for label, kind, pay, s, _w in rows:
        if s != "done":
            continue
        path, old, new = pay
        cur[path] = cur[path].replace(new, old, 1)
    # 与备份对拍
    for path in cur:
        bp = EDBACK.get(path)
        if os.path.exists(bp):
            if cur[path] != read(bp):
                bad("%s 反向替换结果与备份 %s 不一致 ⇒ 拒绝落盘" % (path, bp))
                print("RESULT: FAIL")
                return 1
            ok("%s 反向替换 == 备份（两通道一致）" % os.path.basename(path))
        else:
            info("%s 无备份，仅用反向替换" % os.path.basename(path))
    for path, t in cur.items():
        write(path, t)
        ok("还原 %s" % os.path.basename(path))
    rows, n = report()
    if n.get("old", 0) != len(rows):
        bad("回滚后回读不是原始态（old=%d done=%d ???=%d）"
            % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("回滚后回读 = 原始态（%d/%d）" % (n["old"], len(rows)))
    print("RESULT: PASS")
    return 0


# ---------------------------------------------------------------------------
# verify：红/绿双向，且**全程不碰运行态**
# ---------------------------------------------------------------------------
def count_newer(src, so, exclude_dist):
    """复刻修好后的判据本身（不是复述脚本里那行文本）。"""
    cmd = ["find", src, "-name", "*.go"]
    if exclude_dist:
        cmd += ["-not", "-path", "*/dist/*"]
    cmd += ["-newer", so]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return [x for x in r.stdout.splitlines() if x.strip()]


def do_verify():
    rows, n = report()
    if n.get("done", 0) != len(rows):
        bad("不是目标态（old=%d done=%d ???=%d）" % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("状态 = 目标态（%d 项）" % len(rows))

    # 脚本文本确实改到位（两道）
    src = read(DEPLOY)
    if "-not -path '*/dist/*' -newer" not in src:
        bad("fresh() 的扫描未排除 dist/")
        print("RESULT: FAIL")
        return 1
    if 'diff -rq "$AGT/src_gopher" "$AGT/dist/src_gopher"' not in src:
        bad("do_install 里没有源码副本内容判据")
        print("RESULT: FAIL")
        return 1
    ok("两处改动都在落盘文本里")

    # --- 绿侧 A：修好后的 fresh 判据在当前树上必须**不报** ---
    for so, tree in ((AGTSO, AGT), (LSTSO, LST)):
        if not os.path.exists(so):
            bad("缺 %s" % so)
            print("RESULT: FAIL")
            return 1
        j = count_newer(tree, so, True)
        if j:
            bad("绿侧不成立：%s 排除 dist 后仍有 %d 个更新的 .go：%s" % (so, len(j), j[:5]))
            print("RESULT: FAIL")
            return 1
        ok("绿：%s 排除 dist 后 newer=0" % os.path.basename(so))

    # --- 复现假红（含 dist 时的 27），证明"这是自己的输出"不是猜的 ---
    allnew = count_newer(AGT, AGTSO, False)
    inside = [p for p in allnew if "/dist/" in p]
    if not allnew:
        info("当前树上含 dist 也报 0（源码都比 .so 旧）—— 假红此刻不复现，但结论不受影响")
    elif len(inside) != len(allnew):
        bad("含 dist 时报出 %d 个，其中只有 %d 个在 dist/ 内 ⇒ 还有真·陈旧源文件，"
            "不能靠排除 dist 掩盖：%s" % (len(allnew), len(inside),
                                          [p for p in allnew if "/dist/" not in p][:5]))
        print("RESULT: FAIL")
        return 1
    else:
        ok("假红复现：含 dist ⇒ newer=%d，且**全部**在 dist/ 内（= 构建自己的输出）" % len(allnew))

    # --- 红侧 A：修好的判据必须**照样能红**（在 src_gopher 放一个比 .so 新的探针 .go）---
    probe = os.path.join(AGT, "src_gopher", "__probe_newer.go")
    try:
        write(probe, "package main\n")
        j = count_newer(AGT, AGTSO, True)
        if not j:
            bad("红侧失败：放了比 .so 新的探针 .go，修好的判据却**没报** ⇒ 判据被修瞎了")
            print("RESULT: FAIL")
            return 1
        ok("红：探针 .go 被报出（%s）⇒ 修好的判据仍有判别力" % os.path.basename(j[0]))
    finally:
        if os.path.exists(probe):
            os.remove(probe)

    # --- 绿侧 B：内容判据在当前树上必须一致 ---
    r = subprocess.run(["diff", "-rq", os.path.join(AGT, "src_gopher"),
                        os.path.join(AGT, "dist", "src_gopher")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        bad("绿侧不成立：src_gopher 与 dist/src_gopher 不一致：%s" % (r.stdout or r.stderr)[:300])
        print("RESULT: FAIL")
        return 1
    ok("绿：dist/src_gopher 与 src_gopher 逐文件一致")

    # --- 红侧 B：内容判据必须能红 ---
    extra = os.path.join(AGT, "dist", "src_gopher", "__probe_extra.txt")
    try:
        write(extra, "probe\n")
        r = subprocess.run(["diff", "-rq", os.path.join(AGT, "src_gopher"),
                            os.path.join(AGT, "dist", "src_gopher")],
                           capture_output=True, text=True)
        if r.returncode == 0:
            bad("红侧失败：dist 里多了文件，内容判据却报一致 ⇒ 判据无效")
            print("RESULT: FAIL")
            return 1
        ok("红：dist 多一个文件被报出 ⇒ 内容判据有判别力")
    finally:
        if os.path.exists(extra):
            os.remove(extra)
    if subprocess.run(["diff", "-rq", os.path.join(AGT, "src_gopher"),
                       os.path.join(AGT, "dist", "src_gopher")],
                      capture_output=True, text=True).returncode != 0:
        bad("探针没清干净")
        print("RESULT: FAIL")
        return 1
    ok("探针已清除，两棵树恢复一致")
    print("RESULT: PASS")
    return 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    if mode == "lint":
        es = lint()
        for e in es:
            bad(e)
        if es:
            print("RESULT: FAIL（%d 处自包含/退化锚点）" % len(es))
            return 1
        print("  [ok  ] %d 对锚点均为非自包含" % len(TEXTS))
        print("RESULT: PASS")
        return 0
    if mode == "check":
        print("== 批次7 fresh() 观测量修正：%d 项文本 ==" % len(TEXTS))
        _rows, n = report()
        if n.get("???", 0):
            print("RESULT: FAIL（%d 项歧义）" % n["???"])
            return 1
        print("RESULT: PASS（old=%d done=%d）" % (n.get("old", 0), n.get("done", 0)))
        return 0
    fn = {"apply": do_apply, "revert": do_revert, "verify": do_verify}.get(mode)
    if fn is None:
        bad("未知模式 %r" % mode)
        print("RESULT: FAIL")
        return 2
    return fn()


def entry():
    try:
        return main()
    except KeyboardInterrupt:
        print("\nRESULT: FAIL（被中断）")
        return 130
    except Exception:
        import traceback
        traceback.print_exc()
        print("\nRESULT: FAIL（脚本异常，见上方 traceback）")
        return 1


if __name__ == "__main__":
    sys.exit(entry())
