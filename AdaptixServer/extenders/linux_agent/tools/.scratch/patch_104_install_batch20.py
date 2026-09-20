#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_104_install_batch20.py —— 批次20 装机（BOF 页面动态化 + 上传/编译落地）

为什么是「薄薄一层」：装机机械（内存闸门 / 具名回滚点 / 停 / 换 / 逐件 md5 对账 / 起 /
日志锚定 / 稳态等待）在 patch_102 里已经**被生产验证过三次**（批次15/16/19）。
本脚本只做三件事：换 ARTIFACTS、加一项「退休孤儿 bundle」、加批次20 自己的语义判据。
★ 复用而非重写 —— 重写等于把已验证的机械再赌一次。

本批装什么（29 件 + 3 件退休）：
  1  `extenders/linux_agent/agent_linux.so`      换：ede85bc6… → cdfc9aa5…
  2  `web/index.html`                            换：be2b46dd… → 指向新 bundle
  3  `web/assets/index-Do2LuiQN.js`              **新增**（内含 /api/bofs/list 等三端点）
  26 `extenders/linux_agent/src_agent/bofs/*.c`  **新增**（run 树此前没有这个目录）
  退休 3 个孤儿 bundle（mv 进回滚点，不删）

★★ 第 3 项与那 26 项的关系是本批的**真问题**：
   run 树没有 `src_agent/bofs/` ⇒ 上传路径会 MkdirAll 自愈（所以「上传」看起来能用），
   但 26 个现役 BOF 一律显示「无源码」、单编/全量重编一律失败 ⇒ 「上传能用、编译不能用」。
   根因不在装机，在 `linux_agent/Makefile` 的 dist 规则漏了这一份拷贝（批次20 已修）。
   ⇒ 所以本批装的 26 个 .c **来自修正后的 dist**，与 src 树逐字节一致。

用法: patch_104_install_batch20.py [status|stage|preflight|install|verify|restart|revert]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import patch_102_install_batch15 as p102   # noqa: E402  装机机械
import patch_103_install_batch16 as p103   # noqa: E402  借它的 preflight 形态（EXEMPT/OWNED）

TAG = "batch20"
p102.BAKDIR = "/root/gp5probe/bak20/run_pre_install"
p102.STAGE = "/root/gp5probe/stage20/web"

D = p102.DELIVER
LG = D + "/extenders/linux_agent"
BOFSRC = LG + "/dist/src_agent/bofs"

# 孤儿 bundle：批次19 装的 CJmW6xBx 在批次20 后不再被任何 index.html 指向；
# 另两个是更早的历史遗留。★ 一律 mv 进回滚点，不 rm —— 与本项目「不删交付物」一致。
ORPHANS = [
    ("index-CJmW6xBx.js", "99abd2d7357363431c9b0ac3b6abfd16"),
    ("index-Ceqte44b.js", "d80b393adcf5594a799c05d88697d7f3"),
    ("index-CsbzVo2I.js", "7d95b26600fac528c333a355e5d39cf8"),
]

ART = [
    ("linux_agent_so", "extenders/linux_agent/agent_linux.so",
     LG + "/dist/agent_linux.so", "d",
     "ede85bc662cf2f4f29b4539ea73560fa", "cdfc9aa599ce514b968f6bdd9dce61c4"),
    ("web_index", "web/index.html",
     p102.STAGE + "/index.html", "s",
     "be2b46dd5413b816d7b357243993cf23", "1932c1db7c7b33370031608ff9bb2071"),
    # ★ CSS **不装**：Vite 文件名即内容哈希，run 树现役 `index-DHzY06nb.css`
    #   与本次构建产物 md5 同为 00b145a9… ⇒ 内容一模一样，装它只是徒增一次写。
    ("web_js_b20", "web/assets/index-Do2LuiQN.js",
     p102.STAGE + "/assets/index-Do2LuiQN.js", "s",
     None, "f4ea4b804b568f3d588b2f81d04574d9"),
]

# 26 个 BOF 源码：**新增件**（run 树无此目录）⇒ old 一律 None。
# ★ new 由**交付件现算**而非手抄：交付件若被改动/损坏，这里会立刻对不上而判红，
#   手抄的常量则不会（那正是「判据与事实脱钩」的经典形态）。
if not os.path.isdir(BOFSRC):
    sys.exit("交付件缺失：%s（先跑 linux_agent 的 `GOWORK=off make` 重建 dist）" % BOFSRC)
_bofsrc_names = sorted(n for n in os.listdir(BOFSRC) if n.endswith(".c"))
if len(_bofsrc_names) != 26:
    sys.exit("交付件数量异常：%s 下有 %d 个 .c，期望 26" % (BOFSRC, len(_bofsrc_names)))
for _n in _bofsrc_names:
    _p = os.path.join(BOFSRC, _n)
    ART.append(("bofsrc_" + _n[:-2], "extenders/linux_agent/src_agent/bofs/" + _n,
                _p, "d", None, p102.md5(_p)))

p102.ARTIFACTS = ART
p103.EXEMPT = {}
p103.OWNED = ["linux_agent", "linux_listener_tcp", "gopher_agent", "gopher_listener_tcp"]
p103.MEM_FLOOR_MB = 500

md5 = p102.md5
RUN, BAKDIR = p102.RUN, p102.BAKDIR


# ──────────────────────────────────────────────── 退休孤儿 bundle
def _orphan_state():
    out = []
    for name, want in ORPHANS:
        cur = md5(os.path.join(RUN, "web/assets", name))
        out.append((name, want, cur))
    return out


def cmd_retire():
    """把不再被引用的历史 bundle 移入回滚点。幂等：已移走的不再动。"""
    os.makedirs(BAKDIR, exist_ok=True)
    moved, skipped, bad = [], [], []
    for name, want, cur in _orphan_state():
        if cur is None:
            skipped.append(name)                       # 已退休
        elif cur != want:
            bad.append("%s 现 md5=%s 期望=%s" % (name, cur, want))
        else:
            src = os.path.join(RUN, "web/assets", name)
            dst = os.path.join(BAKDIR, "run__web__assets__" + name)
            os.replace(src, dst)
            moved.append(name)
    print("=== 退休孤儿 bundle ===")
    for n in moved:
        print("  mv → 回滚点  %s" % n)
    for n in skipped:
        print("  已在回滚点  %s" % n)
    for b in bad:
        print("  ★ md5 不符，未动：%s" % b)
    return 1 if bad else 0


def cmd_unretire():
    back = []
    for name, _ in ORPHANS:
        src = os.path.join(BAKDIR, "run__web__assets__" + name)
        if os.path.isfile(src):
            os.makedirs(os.path.join(RUN, "web/assets"), exist_ok=True)
            os.replace(src, os.path.join(RUN, "web/assets", name))
            back.append(name)
    print("=== 孤儿 bundle 复位 ===")
    for n in back:
        print("  mv ← 回滚点  %s" % n)
    return 0


# ──────────────────────────────────────────────── 批次20 语义判据
def extra_judges():
    """md5 对只能证明「装的是交付件」；下面三条证明「装完真的能用」。"""
    bad = 0
    # ★ 日志路径在这里定义一次即可：J2 与 J3 都要用。
    #   上一版把定义留在 J3 块里，而 J2 在它**前面**读 —— Python 见函数体内有赋值
    #   就把 log 当局部名，于是 J2 那句直接 UnboundLocalError（判据自己先崩，
    #   跟被验对象毫无关系）。教训：同一函数里挪动定义位置时，要按**使用顺序**核一遍。
    log = os.path.join(RUN, "server.log")

    # J1 26 个 .c 到位且与交付件逐一致（不只看数量）
    n_ok = n_all = 0
    for label, rel, src, kind, old, new in ART:
        if not label.startswith("bofsrc_"):
            continue
        n_all += 1
        if md5(os.path.join(RUN, rel)) == new:
            n_ok += 1
    print("  J1 BOF 源码 %d/%d 到位且 md5 一致" % (n_ok, n_all))
    bad += (n_ok != n_all)

    # J2 插件真的加载了新代码：本进程日志段里出现路由注册行（旧 .so 没有这一行）
    #    ★ 这条是「换了 .so」与「新功能生效」之间唯一可信的桥：md5 对 ≠ 功能在。
    #    ★★ 初版这里判「该行必须**晚于**最后一次 is ready」——**是错的，且恒红**：
    #       InitPlugin（打路由行）发生在 teamserver 打印 ready 横幅**之前**，
    #       顺序天然是 routes → Starting server → Web UI → is ready。
    #       拿顺序当判据等于把「配置正确」判成红（假红孪生形态）。
    #       正解是 p102.proc_slice()：用**启动横幅**把日志切成「本进程那一段」，
    #       再看标记在不在段内。它自带非空洞自证（anchored_ok），
    #       空切片/锚定失败一律算红 —— 这正是 log_lines_since 被明令禁止当闸门的原因。
    seg, start, ok = p102.proc_slice(log)
    if not ok:
        print("  J2 ★ 日志锚定失败（被截断/轮转）⇒ 红：无法把标记归因到本进程")
        bad += 1
    else:
        key = "bofs routes ready"
        hits = [l for l in seg if key in l]
        print("  J2 本进程日志段 %d 行（自第 %d 行起），'%s' 命中 %d 条"
              % (len(seg), start, key, len(hits)))
        if not hits:
            print("     ★ 段内没有该行 ⇒ 旧 .so 仍在生效，新功能没上线")
            bad += 1
        else:
            print("     %s" % hits[-1].strip()[:120])

    # J3 被服务的 index.html 指向的 bundle 必须在盘上（否则启动自检会退回占位页）
    idx = os.path.join(RUN, "web/index.html")
    try:
        html = open(idx, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print("  J3 读不到 %s：%s" % (idx, e))
        return bad + 1
    refs = []
    for tok in html.split('"'):
        if tok.startswith("/assets/"):
            refs.append(tok)
    missing = [r for r in refs if not os.path.isfile(os.path.join(RUN, "web", r.lstrip("/")))]
    print("  J3 index.html 引用 %d 个资源，缺失 %d 个%s"
          % (len(refs), len(missing), ("：" + str(missing)) if missing else ""))
    bad += (not refs) or bool(missing)

    # J4 孤儿必须已退休（否则 run 树仍会下发含硬编码 18 的旧 bundle）
    left = [n for n, _, cur in _orphan_state() if cur is not None]
    print("  J4 孤儿 bundle 已退休%s" % ("（残留 %s）" % left if left else ""))
    bad += bool(left)

    return bad


def cmd_verify():
    rc = p102.cmd_verify()
    print()
    print("=== 批次20 额外判据（装机件语义）===")
    n = extra_judges()
    if n:
        print("★ 额外判据 %d 条红 ⇒ md5 对但**功能**不对，先别用；必要时 `revert` + `restart`。" % n)
        return 9
    print("额外判据全绿。")
    return rc


def cmd_install():
    print("=== 批次20 装机前闸门 ===")
    print("  交付件核对：%d 件（1 .so + 2 web + %d BOF 源码）"
          % (len(ART), len(_bofsrc_names)))
    # ★ p102._preflight() 返回的是 **(待换列表, 已完成数) 二元组**，不是退出码。
    #   写成 `if pre != 0` 的话二元组恒为真 ⇒ 每次装机都会被自己挡下（本脚本初版
    #   就是这个 bug，靠干跑才发现）。它的契约是「不符预期就自己 SystemExit」，
    #   所以正确用法是「调用，然后什么都不比」。
    p102._preflight()
    print()
    rc = p102.cmd_install()
    if rc != 0:
        return rc
    print()
    rc2 = cmd_retire()
    if rc2:
        print("★ 退休步骤有异常（新件已装好，孤儿未清理）。")
    print()
    return cmd_verify()


def cmd_revert():
    print("=== 批次20 回滚 ===")
    rc = p102.cmd_revert()
    cmd_unretire()
    return rc


def cmd_status():
    rc = p102.cmd_status()
    print()
    print("=== 批次20 孤儿 bundle ===")
    for name, want, cur in _orphan_state():
        print("  %-22s run=%s  期望(退休前)=%s" % (name, cur or "(已退休)", want))
    return rc


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    table = {
        "status": cmd_status,
        "stage": p102.cmd_stage,
        "preflight": p102._preflight,
        "verify": cmd_verify,
        "install": cmd_install,
        "restart": p102.cmd_restart,
        "revert": cmd_revert,
        "retire": cmd_retire,
        "unretire": cmd_unretire,
    }
    if cmd not in table:
        print(__doc__)
        return 1
    return table[cmd]()


if __name__ == "__main__":
    sys.exit(main())
