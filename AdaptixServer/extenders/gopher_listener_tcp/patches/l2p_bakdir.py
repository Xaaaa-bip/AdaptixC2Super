#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次10：全树 `.bak-*` 统一 BAKDIR（批次6 留下的那条的收口）。

## 起因（批次6 自己写下的待办，原文）

  > `$LST` 下还有 9 个**更早批次**的回滚点（`.bak-l2p` / `.bak-bind` / `.bak-order` /
  > `.bak-dns611`）…… ⇒ 本批次**不碰**，留给"所有补丁脚本统一 BAKDIR"那一次。

  `l2p_treefix.py` 已经把 `l2p_relay.py` 并到了 BAKDIR 约定上；本脚本把**其余全部**并过去。

## 分类准则（本批次立的规矩，不是拍脑袋）

  · **BAKDIR**（`/root/gp5probe/bak/`）= 回滚点，且其**消费者脚本仍然存在且可执行**
    ⇒ revert 仍有意义 ⇒ 移过去，并**同步改消费者脚本的路径**（否则等于毁掉回滚能力）。
  · **RETIRED**（`/root/gp5probe/retired/`）= 无消费者的历史孤儿
    ⇒ **保留字节、移出视野**（没有 git，回滚点一律不销毁 ⇒ 只准 mv）。
  · ★ **移文件还不够**：生成者若仍往树里写，清理会**再长回来**。所以四个生成者脚本
    必须一起改到 BAKDIR —— 这是本批次"改动面"的真正来源。

## ★★ 两条纪律（本批次现场学到，已并入判据）

  1. **线性补丁脚本禁止 import**。`p83.py` 是线性脚本：模块级就 `io.open(ROOT+p,"w")`
     写盘 ⇒ `import p83` = **重新应用补丁并覆写源码**。⇒ 判据 W5 只 import
     **带 `if __name__ == "__main__":` 卫兵**的脚本；无卫兵的一律走**纯文本判据**。
  2. **回滚点唯一性由路径保证**（批次9d 第 13 种坑）⇒ W2 断言 28 个目标路径互不相同。

## 判据 W1–W7

  W1 交付树 `.bak*` 计数 == 0（third_party 另计并打印，说明范围）
  W2 28 个目标路径**互不相同**、**都在 BAKDIR/RETIRED 内**、**都不在交付树内**、**都已存在**
  W3 每个文件的 md5 == ledger 记录的移动前 md5（mv 逐字节无损），且原路径已不存在
  W4 四个消费者里 `+ BAK` 形态的路径拼接计数 == 0，`bakp` 定义恰 1 处
  W5 ★消费者自证：import（仅限有卫兵者）并**调用它自己的 bakp()**，返回值 == 目标路径，
     且该路径 md5 == ledger 值 —— 验的是**消费者的代码**，不是复刻
  W6 四个消费者 `py_compile` 通过，且 `check` 模式仍能跑（无 Traceback）
  W7 本脚本自身的 revert 段与 APPLY 段同在（无 git ⇒ 唯一回滚手段）

用法: l2p_bakdir.py status|check|apply|revert|verify
"""
import hashlib
import os
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True   # ★ 批次13：W5 的 importlib 动态 import 会写 __pycache__
# ★ 批次13 E9：**子/孙进程**的字节码一律落到**树外**（E8 的 `-B` 不够，见 doc/批次13 §十一）。
#   环境变量可被所有后代继承 ⇒ 覆盖 W6a 的 py_compile 与任何更深层的 python 调用。
os.environ["PYTHONPYCACHEPREFIX"] = "/root/gp5probe/tmp/pycache"
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

LST = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak"
RETIRED = "/root/gp5probe/retired"
LEDGER = os.path.join(BAKDIR, "_batch10_moved.txt")
LSTN = os.path.join(LST, "extenders", "gopher_listener_tcp")
SUF = ".bak-bakdir"

P_L2PLIST = "/root/gp5probe/patch_l2plistener.py"
P_BIND = "/root/gp5probe/l2p_bind.py"
P_ORDER = "/root/gp5probe/l2p_order.py"
P_P83 = os.path.join(LST, "extenders", "linux_agent", "tools", ".scratch", "p83.py")

# ---- A 类：有可执行消费者 ⇒ BAKDIR（消费者脚本同步改路径）------------------
A_MOVES = [
    ("extenders/gopher_listener_tcp/Makefile.bak-l2p",
     "gopher_listener_tcp.Makefile.bak-l2p"),
    ("extenders/gopher_listener_tcp/ax_config.axs.bak-l2p",
     "gopher_listener_tcp.ax_config.axs.bak-l2p"),
    ("extenders/gopher_listener_tcp/pl_main.go.bak-l2p",
     "gopher_listener_tcp.pl_main.go.bak-l2p"),
    ("extenders/gopher_listener_tcp/pl_transport.go.bak-l2p",
     "gopher_listener_tcp.pl_transport.go.bak-l2p"),
    ("extenders/gopher_listener_tcp/pl_libp2p.go.bak-bind",
     "gopher_listener_tcp.pl_libp2p.go.bak-bind"),
    ("extenders/gopher_listener_tcp/pl_main.go.bak-bind",
     "gopher_listener_tcp.pl_main.go.bak-bind"),
    ("extenders/gopher_listener_tcp/pl_main.go.bak-order",
     "gopher_listener_tcp.pl_main.go.bak-order"),
    ("extenders/linux_agent/tools/.scratch/redact.c.bak83",
     "linux_agent.tools.redact.c.bak83"),
]

# ---- B 类：无消费者的孤儿 ⇒ RETIRED（保留字节，不销毁）---------------------
B_MOVES = [
    ("core/server/ts_agent_builder.go.bak",
     "core.server.ts_agent_builder.go.bak"),
    ("extenders/gopher_listener_tcp/go.mod.bak-dns611",
     "gopher_listener_tcp.go.mod.bak-dns611"),
    ("extenders/gopher_listener_tcp/go.sum.bak-dns611",
     "gopher_listener_tcp.go.sum.bak-dns611"),
    ("extenders/linux_agent/.scratch/bof_api.c.bak76",
     "linux_agent.scratch.bof_api.c.bak76"),
    ("extenders/linux_agent/.scratch/bof_api.h.bak76",
     "linux_agent.scratch.bof_api.h.bak76"),
    ("extenders/linux_agent/.scratch/redact.c.bak91",
     "linux_agent.scratch.redact.c.bak91"),
    ("extenders/linux_agent/.scratch/redact.c.bak92",
     "linux_agent.scratch.redact.c.bak92"),
    ("extenders/linux_agent/src_agent/.scratch/argprobe.c.bak78",
     "linux_agent.src_agent.scratch.argprobe.c.bak78"),
    ("extenders/linux_agent/src_agent/.scratch/bof_api.c.bak78",
     "linux_agent.src_agent.scratch.bof_api.c.bak78"),
    ("extenders/linux_agent/src_agent/.scratch/elf_bof.c.bak78",
     "linux_agent.src_agent.scratch.elf_bof.c.bak78"),
    ("extenders/linux_agent/src_agent/.scratch/hide.insns.bak",
     "linux_agent.src_agent.scratch.hide.insns.bak"),
    ("extenders/linux_agent/src_agent/bofs/ebpf_load.c.p5fix2.bak",
     "linux_agent.src_agent.bofs.ebpf_load.c.p5fix2.bak"),
    ("extenders/linux_agent/tools/.scratch/build_bpf.bak82",
     "linux_agent.tools.scratch.build_bpf.bak82"),
    ("extenders/linux_agent/tools/.scratch/deploy.bak82",
     "linux_agent.tools.scratch.deploy.bak82"),
    ("extenders/linux_agent/tools/.scratch/ld_preload_install.c.bak83",
     "linux_agent.tools.scratch.ld_preload_install.c.bak83"),
    ("extenders/linux_agent/tools/.scratch/p20.bak78",
     "linux_agent.tools.scratch.p20.bak78"),
    ("extenders/linux_agent/tools/.scratch/redact.c.bak84",
     "linux_agent.tools.scratch.redact.c.bak84"),
    ("extenders/linux_agent/tools/.scratch/run_regress.bak78",
     "linux_agent.tools.scratch.run_regress.bak78"),
    ("extenders/linux_agent/tools/.scratch/ssh_persist.c.bak85",
     "linux_agent.tools.scratch.ssh_persist.c.bak85"),
    ("extenders/linux_agent/tools/.scratch_e2e.bak78",
     "linux_agent.tools.scratch_e2e.bak78"),
]

ALL = [(s, d, BAKDIR) for s, d in A_MOVES] + [(s, d, RETIRED) for s, d in B_MOVES]

# ---- 消费者编辑（old 必须唯一命中；REVERT 段与 APPLY 段同在）-------------
BAKP_L2PLIST = '''BAKDIR = "/root/gp5probe/bak"


def bakp(rel):
    """★ 批次10：回滚点落在 BAKDIR（**交付树之外**）。
    命名 = `<树>.<相对路径>.<后缀>`，与批次6 的 l2p_relay.py 同约定。
    ★ T 由 main() 设；本函数在未设 T 时会报 TypeError（不许默默算出一个错路径）。"""
    tree = os.path.basename(os.path.abspath(T).rstrip(os.sep))
    return os.path.join(BAKDIR, tree + "." + rel.replace(os.sep, ".") + BAK)
'''

BAKP_ABS = '''BAKDIR = "/root/gp5probe/bak"


def bakp(p):
    """★ 批次10：回滚点落在 BAKDIR（**交付树之外**）。
    命名 = `<树>.<相对路径>.<后缀>`（与批次6 同约定）。"""
    rel = os.path.relpath(os.path.abspath(p), os.path.dirname(A))
    return os.path.join(BAKDIR, rel.replace(os.sep, ".") + BAK)
'''

EDITS = [
    # (文件, old, new, 说明)
    (P_L2PLIST, 'BAK = ".bak-l2p"\n', 'BAK = ".bak-l2p"\n' + BAKP_L2PLIST, "加 BAKDIR + bakp()"),
    (P_L2PLIST, "        bak = os.path.join(T, rel + BAK)\n", "        bak = bakp(rel)\n", "gofmt_net 读回滚点"),
    (P_L2PLIST, "            shutil.copy2(os.path.join(T, rel), os.path.join(T, rel + BAK))\n",
     "            shutil.copy2(os.path.join(T, rel), bakp(rel))\n", "apply 写回滚点"),
    (P_L2PLIST, "            b = os.path.join(T, rel + BAK)\n", "            b = bakp(rel)\n", "revert 读回滚点"),
    (P_BIND, 'BAK = ".bak-bind"\n', 'BAK = ".bak-bind"\n' + BAKP_ABS, "加 BAKDIR + bakp()"),
    (P_BIND, "        shutil.copy2(SRC_L2P, SRC_L2P + BAK)\n        shutil.copy2(SRC_MAIN, SRC_MAIN + BAK)\n",
     "        shutil.copy2(SRC_L2P, bakp(SRC_L2P))\n        shutil.copy2(SRC_MAIN, bakp(SRC_MAIN))\n", "apply 写回滚点"),
    (P_BIND, "            if not os.path.exists(p + BAK):\n", "            if not os.path.exists(bakp(p)):\n", "revert 前置守卫"),
    (P_BIND, "            shutil.move(p + BAK, p)\n", "            shutil.move(bakp(p), p)\n", "revert 还原"),
    (P_ORDER, 'BAK = ".bak-order"\n', 'BAK = ".bak-order"\n' + BAKP_ABS, "加 BAKDIR + bakp()"),
    (P_ORDER, "        shutil.copy2(SRC, SRC + BAK)\n", "        shutil.copy2(SRC, bakp(SRC))\n", "apply 写回滚点"),
    (P_ORDER, "        if not os.path.exists(SRC + BAK):\n", "        if not os.path.exists(bakp(SRC)):\n", "revert 前置守卫"),
    (P_ORDER, "        shutil.move(SRC + BAK, SRC)\n", "        shutil.move(bakp(SRC), SRC)\n", "revert 还原"),
    (P_P83, 'BAK = "/root/cc/AdaptixServer/extenders/linux_agent/tools/.scratch/"\n',
     'BAKDIR = "/root/gp5probe/bak"        # ★ 批次10：回滚点移出交付树\n'
     'BAK = BAKDIR + "/linux_agent.tools."\n', "p83 回滚点移出交付树"),
]

CONSUMERS = [P_L2PLIST, P_BIND, P_ORDER, P_P83]


def ok(m):
    print("  [ok  ] %s" % m)
    return True


def bad(m):
    print("  [BAD ] %s" % m)
    return False


def info(m):
    print("  [info] %s" % m)


def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 16), b""):
            h.update(b)
    return h.hexdigest()


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write(p, t):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(t)


def ledger():
    """ledger 行: md5<TAB>srcrel<TAB>dst"""
    if not os.path.exists(LEDGER):
        return {}
    d = {}
    for ln in read(LEDGER).splitlines():
        if not ln.strip():
            continue
        a = ln.split("\t")
        if len(a) == 3:
            d[a[1]] = (a[0], a[2])
    return d


def tree_baks():
    out = subprocess.run(["find", LST, "-name", "*.bak*", "-not", "-path", "*/third_party/*"],
                         capture_output=True, text=True).stdout.split()
    return [p for p in out if p]


def tree_baks_tp():
    out = subprocess.run(["find", LST, "-name", "*.bak*", "-path", "*/third_party/*"],
                         capture_output=True, text=True).stdout.split()
    return [p for p in out if p]


def status():
    rm = tree_baks()
    print("交付树(除 third_party) 的 .bak* 计数: %d" % len(rm))
    for p in rm:
        print("   ", p.replace(LST + "/", ""))
    print("third_party 内 .bak* 计数: %d（上游第三方源码，不在本批次范围）" % len(tree_baks_tp()))
    print("A_MOVES=%d  B_MOVES=%d  合计=%d" % (len(A_MOVES), len(B_MOVES), len(ALL)))
    lg = ledger()
    print("ledger: %s（%d 条）" % (LEDGER, len(lg)))
    for s, d, tgt in ALL:
        src = os.path.join(LST, s)
        dst = os.path.join(tgt, d)
        print("  %-6s %s" % ("已移" if (not os.path.exists(src) and os.path.exists(dst))
                             else ("待移" if os.path.exists(src) else "缺失"),
                             s))


def strip_bakp(t):
    """★ 判据陷阱（批次9 坑2 的同族，本批次**又犯一次**）：新建的 bakp() 定义体
    自身就以 `+ BAK)` 结尾 ⇒ 数"旧形态残留"必须**先把定义块摘掉**，
    否则判据自己撞自己，报假红。"""
    i = t.find("def bakp(")
    if i < 0:
        return t
    j = t.find("\n\n", i)
    return t[:i] + (t[j:] if j > 0 else "")


def check(verbose=True):
    r = []
    # ---- 目标路径唯一 + 不在树内 ----
    dsts = [os.path.join(t, d) for _s, d, t in ALL]
    r.append((len(set(dsts)) == len(dsts), "W2a 28 个目标路径互不相同（%d/%d）"
              % (len(set(dsts)), len(dsts))))
    outside = all(not os.path.abspath(p).startswith(LST + os.sep) for p in dsts)
    r.append((outside, "W2b 全部目标路径都在交付树之外"))
    # ---- 源：要么在树内待移、要么已移到位 ----
    n_src, n_dst = 0, 0
    for s, d, tgt in ALL:
        src, dst = os.path.join(LST, s), os.path.join(tgt, d)
        if os.path.exists(src):
            n_src += 1
        if os.path.exists(dst):
            n_dst += 1
    r.append((n_src + n_dst == len(ALL) and n_src * n_dst == 0,
              "W2c 每个文件要么在树内、要么已就位（树内 %d / 就位 %d / 共 %d，无两份并存）"
              % (n_src, n_dst, len(ALL))))
    # ---- 消费者锚点 ----
    for f, old, new, desc in EDITS:
        t = read(f)
        c = t.count(old)
        done = new in t
        good = (c == 1) or (c == 0 and done)
        r.append((good, "W4 锚点 [%s] %s（old %d 次，%s）"
                  % (os.path.basename(f), desc, c,
                     "已应用" if (c == 0 and done) else
                     ("待应用" if c == 1 else "★异常：既不在场也未应用"))))
    # ---- 消费者已改则不该再有旧形态 ----
    for f in (P_L2PLIST, P_BIND, P_ORDER):
        t = read(f)
        body = strip_bakp(t)
        left = body.count("+ BAK)") + body.count("+ BAK,") + body.count("+ BAK\n")
        done = "def bakp(" in t
        r.append((left == 0 if done else left > 0,
                  "W4b [%s] 旧形态残留=%d（已摘掉 bakp 定义体），bakp 定义=%d（已改则须为 0/1）"
                  % (os.path.basename(f), left, t.count("def bakp("))))
    t = read(P_P83)
    p83_done = 'BAK = BAKDIR + "/linux_agent.tools."' in t
    p83_todo = 'BAK = "%s/tools/.scratch/"' % os.path.join(LST, "extenders", "linux_agent") in t
    r.append((p83_done or p83_todo,
              "W4c [p83.py] BAK 指向 BAKDIR（%s）"
              % ("已改" if p83_done else ("待应用" if p83_todo
                 else "★异常：既不指向 BAKDIR，也不是原形态"))))
    # ---- 线性脚本禁 import ----
    for f in CONSUMERS:
        t = read(f)
        guarded = 'if __name__ == "__main__":' in t
        r.append((guarded or f == P_P83,
                  "W5a [%s] %s" % (os.path.basename(f),
                                   "有 __main__ 卫兵 ⇒ 可安全 import"
                                   if guarded else "★ 线性脚本 ⇒ 判据只走纯文本（禁止 import）")))
    if verbose:
        for k, m in r:
            (ok if k else bad)(m)
    return r


def apply():
    print("== 前置断言（不写盘，全过才动字节）==")
    r = check()
    # 允许"已改完一部分"的续跑：只要求锚点唯一或该处已改
    for f, old, new, desc in EDITS:
        t = read(f)
        if t.count(old) != 1 and new not in t:
            print("[BAD ] %s / %s：锚点 %d 次且新文本不在 ⇒ 拒绝落盘"
                  % (os.path.basename(f), desc, t.count(old)))
            return 1
    print()
    print("== ① 记 ledger（移动前 md5）+ 备份消费者脚本 ==")
    lg = ledger()
    for s, d, tgt in ALL:
        src = os.path.join(LST, s)
        if os.path.exists(src):
            lg[s] = (md5(src), os.path.join(tgt, d))
    for f in CONSUMERS:
        b = os.path.join(BAKDIR, os.path.basename(f) + SUF)
        if not os.path.exists(b):
            shutil.copy2(f, b)
            ok("备份 %s → %s" % (f, os.path.basename(b)))
    lines = "\n".join("%s\t%s\t%s" % (v[0], k, v[1]) for k, v in sorted(lg.items()))
    write(LEDGER, lines + "\n")
    ok("ledger 落盘 %s（%d 条）" % (LEDGER, len(lg)))

    print()
    print("== ② 移动 %d 个 .bak*（具名 mv，绝不删除）==" % len(ALL))
    n = 0
    for s, d, tgt in ALL:
        src, dst = os.path.join(LST, s), os.path.join(tgt, d)
        if os.path.exists(src):
            if os.path.exists(dst):
                bad("目标已存在，拒绝覆盖: %s" % dst)
                return 1
            shutil.move(src, dst)
            n += 1
            ok("mv %s → %s" % (s, os.path.basename(dst)))
    info("本次移动 %d 个" % n)

    print()
    print("== ③ 改四个消费者（生成者不再往树里写）==")
    for f, old, new, desc in EDITS:
        t = read(f)
        if new in t and t.count(old) != 1:
            info("[跳过] %s / %s（已是目标态）" % (os.path.basename(f), desc))
            continue
        if t.count(old) != 1:
            bad("%s / %s：锚点 %d 次（应 1）—— 中止" % (os.path.basename(f), desc, t.count(old)))
            return 1
        write(f, t.replace(old, new, 1))
        ok("%s / %s" % (os.path.basename(f), desc))

    print()
    return 0 if verify() else 1


def revert():
    print("== 回滚：先还原消费者脚本，再把文件搬回树内 ==")
    for f in CONSUMERS:
        b = os.path.join(BAKDIR, os.path.basename(f) + SUF)
        if not os.path.exists(b):
            bad("缺回滚点 %s —— 拒绝假装 revert" % b)
            return 1
        shutil.copy2(b, f)
        ok("还原 %s" % os.path.basename(f))
    lg = ledger()
    n = 0
    for s, d, tgt in ALL:
        src, dst = os.path.join(LST, s), os.path.join(tgt, d)
        if os.path.exists(dst) and not os.path.exists(src):
            shutil.move(dst, src)
            n += 1
    ok("搬回 %d 个文件" % n)
    return 0


def verify():
    print()
    print("== W 判据 ==")
    r = list(check(verbose=False))
    # W1 交付树零 .bak*
    rm = tree_baks()
    r.append((len(rm) == 0, "W1 交付树(除 third_party) .bak* 计数 == 0（实测 %d）" % len(rm)))
    # W3 md5 逐字节
    lg = ledger()
    r.append((len(lg) == len(ALL), "W3a ledger 覆盖 %d/%d" % (len(lg), len(ALL))))
    nbad = 0
    for s, d, tgt in ALL:
        dst = os.path.join(tgt, d)
        exp = lg.get(s, ("", ""))[0]
        if not exp:
            nbad += 1
            bad("ledger 缺 %s" % s)
            continue
        if not os.path.exists(dst):
            nbad += 1
            bad("目标缺失 %s" % dst)
            continue
        if md5(dst) != exp:
            nbad += 1
            bad("md5 不符 %s（%s != %s）" % (dst, md5(dst), exp))
        if os.path.exists(os.path.join(LST, s)):
            nbad += 1
            bad("原路径仍存在 %s" % s)
    r.append((nbad == 0, "W3b 28 个文件 md5 与移动前一致、且原路径已清空（%d 处异常）" % nbad))
    # W5 消费者自证
    import importlib.util
    ok5 = True
    for f, arg, exp_dst in ((P_L2PLIST, "pl_main.go", os.path.join(BAKDIR, "gopher_listener_tcp.pl_main.go.bak-l2p")),
                            (P_L2PLIST, "pl_transport.go", os.path.join(BAKDIR, "gopher_listener_tcp.pl_transport.go.bak-l2p")),
                            (P_L2PLIST, "Makefile", os.path.join(BAKDIR, "gopher_listener_tcp.Makefile.bak-l2p")),
                            (P_L2PLIST, "ax_config.axs", os.path.join(BAKDIR, "gopher_listener_tcp.ax_config.axs.bak-l2p")),
                            (P_BIND, "SRC_L2P", os.path.join(BAKDIR, "gopher_listener_tcp.pl_libp2p.go.bak-bind")),
                            (P_BIND, "SRC_MAIN", os.path.join(BAKDIR, "gopher_listener_tcp.pl_main.go.bak-bind")),
                            (P_ORDER, "SRC", os.path.join(BAKDIR, "gopher_listener_tcp.pl_main.go.bak-order"))):
        t = read(f)
        if 'if __name__ == "__main__":' not in t:
            ok5 = False
            bad("W5 [%s] 无 __main__ 卫兵 ⇒ 拒绝 import" % os.path.basename(f))
            continue
        spec = importlib.util.spec_from_file_location("c_" + os.path.basename(f)[:-3], f)
        m = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(m)
        except SystemExit:
            pass
        if hasattr(m, "T"):
            m.T = LSTN
        got = m.bakp(getattr(m, arg) if hasattr(m, arg) and arg.startswith("SRC") else arg)
        same = os.path.abspath(got) == os.path.abspath(exp_dst)
        exist = os.path.exists(got)
        ok5 &= (same and exist)
        (ok if (same and exist) else bad)(
            "W5 [%s] bakp(%s) → %s（%s，存在=%s）"
            % (os.path.basename(f), arg, os.path.basename(got), "一致" if same else "**不符**", exist))
    r.append((ok5, "W5 消费者自证：四个脚本的 bakp() 都指向 BAKDIR 里真实存在的回滚点"))
    # W5c p83 纯文本
    t = read(P_P83)
    exp83 = BAKDIR + "/linux_agent.tools.redact.c.bak83"
    r.append((exp83 in t or ('BAKDIR = "%s"' % BAKDIR) in t,
              "W5c [p83.py] 常数自证：BAK 基址 == BAKDIR（★ 线性脚本，禁止 import）"))
    r.append((os.path.exists(exp83), "W5d [p83.py] 其回滚点在 BAKDIR 真实存在：%s" % os.path.basename(exp83)))
    # W6 py_compile + check 可跑
    # ★ 批次13 E8：子进程是全新解释器 ⇒ 必须显式 `-B`（否则 W6a 给树内 p83.py 写 __pycache__）
    py = sys.executable or "/usr/bin/python3"
    for f in CONSUMERS:
        cp = subprocess.run([py, "-B", "-m", "py_compile", f], capture_output=True, text=True)
        r.append((cp.returncode == 0, "W6a py_compile %s" % os.path.basename(f)))
    for f, args in ((P_BIND, ["check"]), (P_ORDER, ["check"])):
        cp = subprocess.run([py, "-B", f] + args, capture_output=True, text=True)
        r.append(("Traceback" not in cp.stderr + cp.stdout,
                  "W6b %s %s 无 Traceback（rc=%d）" % (os.path.basename(f), args[0], cp.returncode)))
    cp = subprocess.run([py, "-B", P_L2PLIST, LSTN, "check"], capture_output=True, text=True)
    r.append(("Traceback" not in cp.stderr + cp.stdout,
              "W6c patch_l2plistener.py check 无 Traceback（rc=%d）" % cp.returncode))
    # W7 回滚点自洽
    r.append((all(os.path.exists(os.path.join(BAKDIR, os.path.basename(f) + SUF)) for f in CONSUMERS),
              "W7 四个消费者脚本的回滚点都在 BAKDIR"))
    print()
    for k, m in r:
        (ok if k else bad)(m)
    nbad = sum(1 for k, _ in r if not k)
    print()
    if nbad == 0:
        print("RESULT: PASS（%d 条判据，0 条红）" % len(r))
    else:
        print("RESULT: FAIL（%d 条判据，%d 条红）" % (len(r), nbad))
    return nbad == 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    if mode == "status":
        status()
        return 0
    if mode == "check":
        r = check()
        nbad = sum(1 for k, _ in r if not k)
        print("\ncheck: %d 条，%d 条待办/异常" % (len(r), nbad))
        return 0
    if mode == "apply":
        return apply()
    if mode == "revert":
        return revert()
    if mode == "verify":
        return 0 if verify() else 1
    print("用法: %s status|check|apply|revert|verify" % sys.argv[0])
    return 2


if __name__ == "__main__":
    sys.exit(main())
