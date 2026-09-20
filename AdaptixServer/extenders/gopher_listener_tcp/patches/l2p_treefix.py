#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次6：部署链解阻 —— 回滚点移出交付树 + 把 pkgsanity 提前到源码树。

## 起因（实测真事件，不是推演）

批次5 收尾扫四棵树时发现 `gopher_agent/src_gopher/transport_libp2p.go.bak-relay`
（`l2p_relay.py` 的**回滚点**）。`gopher_agent/Makefile` 第 12 行是

    @ cp -r src_gopher ./dist/src_gopher

⇒ 回滚点被**一起带进交付树** ⇒ `l2p_deploy.sh` 的 `pkgsanity "$AGT/dist/src_gopher"`
必然 die（"待投递树里混进了备份/中间文件"）⇒ **整条部署链被自己的门拦死**。

★ **门是对的，树是错的。** `pkgsanity` 拦它的理由正是 M4 写下那条：`.bak-*` 是
**变更记录级痕迹** —— 它把"这个载荷的转发被改过 + 改之前长什么样"连同交付物交给
operator。**绝不能为了让门过而把门改松。**

## 本脚本做两件事

**① 回滚点搬家**：`l2p_relay.py` 的 `bak()` 从 `<源码旁边>.bak-relay` 改到 `BAKDIR`
（`/root/gp5probe/bak/`，**交付树之外**）。★ 这不是新发明 —— `/root/gp5probe/bak/`
里本来就有 6 个同约定的文件（`gopher_listener_tcp.pl_main.go.orig` 等），
本改动只是把 `l2p_relay.py` 并到那条既有约定上。
已存在的 4 个 `.bak-relay` **mv 过去**（不是删除 —— 没有 git，回滚点不能销毁）。

**② 门提前到源头**：`l2p_deploy.sh` 的 `pre()` 里对 **`$AGT/src_gopher`** 再跑一次
`pkgsanity`。理由：`dist/src_gopher` 是 `cp -r` 派生出来的 ⇒ 查源码等于查两者，
而且**在 `make` 之前**就红、理由也对（原路径要等 make 跑完才红，报的还是 dist，
看不出"谁把它带进来的"）。

## 顺带清理（mv 到退休区，不是删除）

- `$RAGT/src_gopher/utils/crypt.go.bak-chacha` —— 运行态树里的孤儿回滚点
- `$LST/patches/__pycache__/` —— 批次4 清的是 `pycache-patches/`

## 不动的（如实登记）

`$LST` 下还有 9 个**更早批次**的回滚点（`.bak-l2p` / `.bak-bind` / `.bak-order` /
`.bak-dns611`）。它们**够不到交付树**（listener 的 `dist/` 只装 `config.yaml` +
`ax_config.axs` + `.so`，没有源码副本），且分属其它补丁脚本。
⇒ 本批次**不碰**，留给"所有补丁脚本统一 BAKDIR"那一次。

## ★★★ 本脚本第一版连栽三跤（全部由**落盘后回读**当场抓住）——三条判据纪律

**1. 自包含锚点（栽了两次）。** `A_RELAY` 与 `A_PRE` 的新文本都**以旧文本为前缀**
（我把锚点那行原样保留在开头再往下接注释）。落盘后 `count(旧)==1 且 count(新)==1`
⇒ 状态机把自己的**成功**判成 `???`（部分施加）。
★ 关键是**第二次**：我明明在文件头写下了这条教训、修好了 `A_RELAY`，**却忘了用同一条
尺子量 `A_PRE`**。所以修法不能是"下次更小心"，必须是**机器判据**：
落盘前先自检 `old in new`（见 `lint()`），命中就拒绝并指名那一对。
**纪律：凡"替换"，先证明新文本不含旧文本；人不可靠，用 lint。**
★ 二次修法的要点：把锚点**锚在接缝上**（旧文本末尾跨进下一块的首行 `  # ★ D3（批次4）：`）
⇒ 中间插了东西，旧接缝在新文本里不再连续出现 ⇒ 必然 `旧=0 新=1`。只把空行并进锚点**没用**。

**2. 全局死门会把"中途失败"升级成"树被锁死"。** 第一版 `state()` 要求**全部**项同为
`old` 或同为 `done`，否则 `???` ⇒ apply 与 revert **双双拒绝**，树卡在中间态，只能手救。
★ 这与本项目批次21 记的「**原子 ≠ 串行**」是同一族：**一个全局不变量不该被用来给
逐项操作当门**。⇒ 改成**逐项判定 + 逐项幂等**：apply 跳过已 `done` 的项、revert 跳过
已 `old` 的项，只有**单项自身**歧义（`旧=1 新=1`）才拒绝。中途失败从此只是"跑第二遍"。

**3. 备份备错了对象。** 第一版 `SB = SELF + ".bak"` 备的是**脚本自己**，而 revert 要还原的
是**被改的那两个文件** ⇒ 毫无用处。修法：备 `EDITS` 里每个被改文件，落 `BAKDIR`
（交付树之外，自己的规矩自己守），且 revert 走**反向替换为主 + 与备份对拍**
（两个观测通道不一致就拒绝落盘，不静默二选一）。

用法：
    l2p_treefix.py check    逐项诊断
    l2p_treefix.py lint     只跑锚点自检（不碰任何文件）
    l2p_treefix.py apply    施加（幂等，可重入）
    l2p_treefix.py revert   回滚（幂等；把 .bak-relay mv 回源码旁边）
    l2p_treefix.py verify   施加后验收（含 pkgsanity 红/绿双向）
"""
import os
import shutil
import subprocess
import sys

EXT = "/root/cc/AdaptixServer/extenders"
LST = os.path.join(EXT, "gopher_listener_tcp")
AGT = os.path.join(EXT, "gopher_agent")
RAGT = "/root/cc/run/extenders/gopher_agent"
BAKDIR = "/root/gp5probe/bak"
RETIRED = "/root/gp5probe/retired"
DEPLOY = os.path.join(LST, "patches", "l2p_deploy.sh")
RELAY = os.path.join(LST, "patches", "l2p_relay.py")


def ok(m):
    print("  [ok  ] %s" % m)


def bad(m):
    print("  [BAD ] %s" % m)


def info(m):
    print("  [info] %s" % m)


# ---------------------------------------------------------------------------
# ① 文本改动。★ 新文本**不得包含**旧文本（lint() 强制；见文件头纪律1）
# ---------------------------------------------------------------------------
A_RELAY_OLD = 'BAK = ".bak-relay"\n'
A_RELAY_NEW = '''BAK = ".bak-relay"          # 回滚点**后缀**；目录见 BAKDIR（批次6：移出交付树）
# ★ 批次6（部署链解阻）：回滚点**不许落在交付树里**。
#   实测真事件：`src_gopher/transport_libp2p.go.bak-relay` 被 `Makefile` 第 12 行的
#   `cp -r src_gopher ./dist/src_gopher` 一起带进 dist ⇒ `l2p_deploy.sh` 的 `pkgsanity`
#   当场 die ⇒ **整条部署链被自己的门拦死**。
#   ★ 门是对的，树是错的：`.bak-*` 是**变更记录级痕迹**（把"这个转发被改过 + 改之前
#   长什么样"连同交付物交给 operator）。**不要为了让门过而把门改松。**
#   ⇒ 回滚点改放 BAKDIR（交付树之外），命名沿用本机既有约定 `<树>.<相对路径>.<后缀>`
#   —— `/root/gp5probe/bak/` 里已有 6 个同约定的文件。**是 mv 不是 delete**：
#   没有 git，回滚点就是唯一回滚手段。
BAKDIR = "/root/gp5probe/bak"
'''

A_BAK_OLD = '''def bak(path):
    return path + BAK
'''
A_BAK_NEW = '''def bak(path):
    """回滚点路径。★ 批次6：放 BAKDIR（**交付树之外**），命名 = `<树>.<相对路径>.<后缀>`，
    沿用 /root/gp5probe/bak/ 里既有的约定（如 `gopher_listener_tcp.pl_main.go.orig`）。
    ★ 只用 basename 会撞车：`gopher_agent/pl_main.go` 与 `gopher_listener_tcp/pl_main.go`
    都真实存在；带上树名与相对路径才是无歧义的。
    """
    rel = os.path.relpath(path, os.path.dirname(LST))
    return os.path.join(BAKDIR, rel.replace(os.sep, ".") + BAK)
'''

# ★ A_PRE 锚在**接缝**上：旧文本末尾跨进下一块的首行 ⇒ 新文本中间插了东西，
#   旧接缝在新文本里不再连续出现 ⇒ 落盘后必然 旧=0 新=1。
A_PRE_OLD = '  ok "listener 源码无 DIAG 插桩"\n\n  # ★ D3（批次4）：'
A_PRE_NEW = '''  ok "listener 源码无 DIAG 插桩"

  # ★ 批次6：`pkgsanity` 提前到**源码树**，不再只查 dist 的副本。
  #   实测真事件：`src_gopher/transport_libp2p.go.bak-relay` 被 `Makefile` 的
  #   `cp -r src_gopher ./dist/src_gopher` 一起带进交付树。
  #   dist 是**派生**的 ⇒ 查源码等于查两者，而且**在 make 之前**就红、
  #   理由也对（原路径要等 make 跑完，且报的是 dist，看不出"谁把它带进来的"）。
  pkgsanity "$AGT/src_gopher"
  ok "agent 源码树无备份/中间文件（它们会被 cp -r 带进交付树）"

  # ★ D3（批次4）：'''

TEXTS = [(RELAY, A_RELAY_OLD, A_RELAY_NEW),
         (RELAY, A_BAK_OLD, A_BAK_NEW),
         (DEPLOY, A_PRE_OLD, A_PRE_NEW)]

# ---------------------------------------------------------------------------
# ② 文件搬迁：(源, 目的, 说明)。APPLY = 源→目的；REVERT = 目的→源。
# ---------------------------------------------------------------------------
MOVES = [
    (os.path.join(AGT, "src_gopher", "transport_libp2p.go.bak-relay"),
     os.path.join(BAKDIR, "gopher_agent.src_gopher.transport_libp2p.go.bak-relay"),
     "回滚点：会被 cp -r 带进交付树 ⇒ pkgsanity die"),
    (os.path.join(LST, "pl_libp2p.go.bak-relay"),
     os.path.join(BAKDIR, "gopher_listener_tcp.pl_libp2p.go.bak-relay"),
     "回滚点：统一搬进 BAKDIR（与 agent 侧同规矩）"),
    (os.path.join(LST, "pl_main.go.bak-relay"),
     os.path.join(BAKDIR, "gopher_listener_tcp.pl_main.go.bak-relay"),
     "回滚点：同上"),
    (os.path.join(LST, "pl_transport.go.bak-relay"),
     os.path.join(BAKDIR, "gopher_listener_tcp.pl_transport.go.bak-relay"),
     "回滚点：同上"),
    (os.path.join(AGT, "dist", "src_gopher", "transport_libp2p.go.bak-relay"),
     os.path.join(RETIRED, "dist-src_gopher.transport_libp2p.go.bak-relay"),
     "派生副本里的同一份回滚点（下次 make 会整棵重建）"),
    (os.path.join(RAGT, "src_gopher", "utils", "crypt.go.bak-chacha"),
     os.path.join(RETIRED, "run-src_gopher.utils.crypt.go.bak-chacha"),
     "运行态树里的孤儿回滚点（源码侧对应物批次4 已退休）"),
    (os.path.join(LST, "patches", "__pycache__"),
     os.path.join(RETIRED, "patches__pycache__"),
     "批次4 清的是 pycache-patches/，标准 __pycache__ 是之后跑出来的"),
]

EDBACK = {}
for _p, _o, _n in TEXTS:
    EDBACK.setdefault(_p, os.path.join(BAKDIR, os.path.basename(_p) + ".bak-treefix"))


def read(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def write(p, t):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(t)


def lint():
    """★ 纪律1 的机器判据：新文本包含旧文本 ⇒ 落盘后必然 `旧=1 新=1`，状态机自锁。
    在任何字节落盘**之前**拒绝。"""
    errs = []
    for i, (path, old, new) in enumerate(TEXTS):
        if old in new:
            errs.append("TEXTS[%d] %s：**新文本包含旧文本** ⇒ 落盘后 旧=1 新=1，"
                        "回读必判 ???（脚本第一版两次栽在这）" % (i, os.path.basename(path)))
        if old == new:
            errs.append("TEXTS[%d] %s：新旧一样" % (i, os.path.basename(path)))
    return errs


def tstate(path, old, new):
    """单项文本项状态：old(未施加) / done(已施加) / ???(歧义)"""
    if not os.path.exists(path):
        return "???", "缺文件"
    t = read(path)
    no, nn = t.count(old), t.count(new)
    if no == 1 and nn == 0:
        return "old", None
    if no == 0 and nn == 1:
        return "done", None
    return "???", "锚点命中 旧=%d 新=%d（要求恰好一个为 1）" % (no, nn)


def mstate(src, dst):
    se, de = os.path.exists(src), os.path.exists(dst)
    if se and not de:
        return "old", None
    if de and not se:
        return "done", None
    return "???", "源在=%s 目的在=%s（要求恰好一个）" % (se, de)


def items():
    """→ [(标签, 种类, 载荷, 状态, 理由)]，逐项（★ 不再有全局门）"""
    out = []
    for path, old, new in TEXTS:
        s, w = tstate(path, old, new)
        out.append(("%s :: %s" % (os.path.basename(path), old.split("\n")[0][:44]),
                    "text", (path, old, new), s, w))
    for src, dst, _why in MOVES:
        s, w = mstate(src, dst)
        out.append(("mv %s" % os.path.basename(src), "move", (src, dst, _why), s, w))
    return out


def report():
    rows = items()
    n = {}
    for label, _k, _p, s, w in rows:
        n[s] = n.get(s, 0) + 1
        print("  [%-4s] %s%s" % (s, label, ("  ← %s" % w) if w else ""))
    print("  ---- old=%d done=%d ???=%d ----" % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
    return rows, n


def _backup(path):
    """落盘前把**被改的文件**存进 BAKDIR（★ 不是备脚本自己）。已存在则不覆盖
    —— 它必须一直是"最初的原始形态"，revert 要对拍的就是它。"""
    bp = EDBACK[path]
    if not os.path.exists(bp):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(path, bp)
        ok("被改文件回滚点 → %s" % bp)


def do_apply():
    if lint():
        for e in lint():
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
        if kind == "text":
            path, old, new = pay
            t = read(path)
            if t.count(old) != 1:            # 二道保险（report 与落盘之间有窗口）
                bad("%s 锚点在落盘前一刻命中 %d 次 ⇒ 中止" % (label, t.count(old)))
                print("RESULT: FAIL")
                return 1
            _backup(path)
            write(path, t.replace(old, new, 1))
            ok("写入 %s（%s）" % (os.path.basename(path), label.split(":: ")[-1][:30]))
        else:
            src, dst, why = pay
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(src, dst)
            ok("mv %-42s → %s" % (os.path.basename(src), dst))
    rows, n = report()                       # ★ 落盘后回读
    if n.get("done", 0) != len(rows):
        bad("落盘后回读不是目标态（old=%d done=%d ???=%d）"
            % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("落盘后回读 = 目标态（%d/%d 项 done）" % (n["done"], len(rows)))
    print("RESULT: PASS")
    return 0


def do_revert():
    rows, n = report()
    if n.get("???", 0):
        bad("有 %d 项歧义 ⇒ 拒绝回滚（逐项修好后再来，不会锁树）" % n["???"])
        print("RESULT: FAIL")
        return 1
    if n.get("done", 0) == 0:
        ok("全部已是原始态")
        print("RESULT: PASS")
        return 0
    # 先算后写：文本项全部反向替换成功、且与备份对拍一致，才动盘
    plan = []
    for label, kind, pay, s, _w in rows:
        if s != "done":
            info("跳过（已 old）：%s" % label)
            continue
        if kind == "text":
            path, old, new = pay
            t = read(path)
            if t.count(new) != 1:
                bad("%s 反向锚点在落盘前一刻命中 %d 次 ⇒ 中止" % (label, t.count(new)))
                print("RESULT: FAIL")
                return 1
            back = t.replace(new, old, 1)
            bp = EDBACK.get(path)
            if os.path.exists(bp):
                if back != read(bp):
                    bad("%s 反向替换结果与备份 %s 不一致 ⇒ 拒绝落盘" % (path, bp))
                    print("RESULT: FAIL")
                    return 1
                ok("%s 反向替换 == 备份（两通道一致）" % os.path.basename(path))
            else:
                info("%s 无备份，仅用反向替换" % os.path.basename(path))
            plan.append(("text", path, back))
        else:
            src, dst, _why = pay
            plan.append(("move", dst, src))
    for kind, a, b in plan:
        if kind == "text":
            write(a, b)
            ok("还原 %s" % os.path.basename(a))
        else:
            shutil.move(a, b)
            ok("mv 回 %s" % os.path.basename(b))
    rows, n = report()
    if n.get("old", 0) != len(rows):
        bad("回滚后回读不是原始态（old=%d done=%d ???=%d）"
            % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("回滚后回读 = 原始态（%d/%d 项 old）" % (n["old"], len(rows)))
    print("RESULT: PASS")
    return 0


# ---------------------------------------------------------------------------
# verify：判据本身要能红也能绿
# ---------------------------------------------------------------------------
JUNK = (r"""find "%s" \( -name '*.bak' -o -name '*.bak-*' -o -name '*.orig' """
        r"""-o -name '*.pyc' -o -name '*~' -o -name '*.rej' \) 2>/dev/null""")


def junk_in(d):
    r = subprocess.run(["bash", "-c", JUNK % d], capture_output=True, text=True)
    return [x for x in r.stdout.splitlines() if x.strip()]


def do_verify():
    rows, n = report()
    if n.get("done", 0) != len(rows):
        bad("不是目标态（old=%d done=%d ???=%d）" % (n.get("old", 0), n.get("done", 0), n.get("???", 0)))
        print("RESULT: FAIL")
        return 1
    ok("状态 = 目标态（%d 项）" % len(rows))

    # --- 绿：两棵树都干净 ---
    for d in (os.path.join(AGT, "src_gopher"), os.path.join(AGT, "dist", "src_gopher")):
        if not os.path.isdir(d):
            bad("缺目录 %s" % d)
            print("RESULT: FAIL")
            return 1
        j = junk_in(d)
        if j:
            bad("%s 仍有杂物：%s" % (d, j))
            print("RESULT: FAIL")
            return 1
        ok("干净：%s" % d)

    # --- 红：谓词必须抓得到（探针目录，不碰真树）---
    # ★ 不用 /root/gp5probe/probe —— 那是个 **18.7MB 的 ELF**（5a′ 阶段那个
    #   "给出假数字 18.7MB" 的 libp2p 探针二进制），是已归档教训的物证，不能动。
    probe = "/root/gp5probe/treefix_probe/junktree"
    shutil.rmtree(probe, ignore_errors=True)
    os.makedirs(probe, exist_ok=True)
    write(os.path.join(probe, "a.go"), "package a\n")
    j0 = junk_in(probe)
    if j0:
        bad("探针目录本该干净却报出 %s ⇒ 谓词口径不对" % j0)
        shutil.rmtree(probe, ignore_errors=True)
        print("RESULT: FAIL")
        return 1
    write(os.path.join(probe, "transport_libp2p.go.bak-relay"), "x\n")
    j1 = junk_in(probe)
    shutil.rmtree(probe, ignore_errors=True)
    if not j1:
        bad("往探针树里放了 .bak-relay 而谓词**没抓着** ⇒ 谓词失效，红线不算数")
        print("RESULT: FAIL")
        return 1
    ok("谓词有判别力（污染探针树被报出：%s）" % os.path.basename(j1[0]))

    # --- bak() 落到 BAKDIR，四个名字互不撞车 ---
    src = read(RELAY)
    if 'BAKDIR = "/root/gp5probe/bak"' not in src \
       or 'rel.replace(os.sep, ".") + BAK' not in src:
        bad("l2p_relay.py 的 bak() 未改到 BAKDIR/树名+相对路径")
        print("RESULT: FAIL")
        return 1
    ns = [os.path.join(BAKDIR, x) for x in
          ("gopher_agent.src_gopher.transport_libp2p.go.bak-relay",
           "gopher_listener_tcp.pl_libp2p.go.bak-relay",
           "gopher_listener_tcp.pl_main.go.bak-relay",
           "gopher_listener_tcp.pl_transport.go.bak-relay")]
    miss = [p for p in ns if not os.path.exists(p)]
    if miss:
        bad("BAKDIR 里缺回滚点：%s ⇒ revert 会断" % miss)
        print("RESULT: FAIL")
        return 1
    ok("%d 个回滚点都在 BAKDIR 且名字互不相同" % len(ns))

    # --- 回滚点**内容**必须仍是原始形态（搬家不能改内容）---
    a = read(ns[0])
    if "WithAllowLimitedConn" in a or "EnableRelay" in a:
        bad("BAKDIR 里的 transport_libp2p.go.bak-relay 含 relay 片段 ⇒ 它不是回滚点")
        print("RESULT: FAIL")
        return 1
    ok("回滚点内容仍是原始形态（不含 relay 片段）")

    # --- 原路径必须空出来（否则 Makefile 的 cp -r 又会带进去）---
    for p in [s for s, _d, _w in MOVES if "src_gopher" in s]:
        if os.path.exists(p):
            bad("源码树里仍存在 %s ⇒ 仍会被 cp -r 带进交付树" % p)
            print("RESULT: FAIL")
            return 1
    ok("源码树里的回滚点原路径已全部让空")

    # --- 门真的接上了：污染真树的**源码**侧 ⇒ pre() 必须 die 且理由是 pkgsanity ---
    probe_file = os.path.join(AGT, "src_gopher", "__probe.bak-x")
    try:
        write(probe_file, "probe\n")
        r = subprocess.run(["bash", DEPLOY, "build"], capture_output=True, text=True,
                           cwd=LST, timeout=180)
        blob = (r.stdout or "") + (r.stderr or "")
        if r.returncode == 0:
            bad("源码树被污染而 `l2p_deploy.sh build` **仍然成功** ⇒ 门没接上（假绿）")
            print("RESULT: FAIL")
            return 1
        if "备份/中间文件" not in blob:
            bad("build 失败了但理由不是 pkgsanity：%s" % blob[-500:])
            print("RESULT: FAIL")
            return 1
        ok("红侧成立：源码树被污染 ⇒ build 在 pre() 里 die 且理由是 pkgsanity")
    finally:
        if os.path.exists(probe_file):
            os.remove(probe_file)
    if junk_in(os.path.join(AGT, "src_gopher")):
        bad("探针文件没清干净")
        print("RESULT: FAIL")
        return 1
    ok("探针已清除，源码树重新干净")
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
        print("== 批次6 树卫生：%d 项文本 + %d 项搬迁（逐项）==" % (len(TEXTS), len(MOVES)))
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
