#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Phase5 · 批次13：交付树**非 .bak 杂物**清理（残渣子集）
2026-09-17  服务器 154.198.162.34

  status  三态普查（待移 / 已移 / ★异常）—— ★ **机器对账，不手工转录**
          （批次10「现场偏差1」抄错 1 条的教训：清单必须由机器算出来）
  check   前置断言（**不写盘**；7 处锚点唯一命中、目标路径互不相同，才允许动字节）
  apply   ① 前置断言 → ② 备份 7 个生成者 → BAKDIR + 记 ledger（移动前 md5）
          → ③ 具名 mv → RETIRED → ④ 改生成者（否则清完长回来）
  revert  还原 7 个生成者 + 按 ledger 具名搬回
  verify  判据 N1–N12

★ 与批次10 的分工（两者合起来才是"交付树无历史物"）：
  批次10 管 **回滚点** `.bak*`（28 件出树 + 4 个生成者改道，判据 W1–W7 常绿）；
  本批次管 **残渣**：`__pycache__/`、`*.pyc`、一次性 `*.log`、历史备份 `*.pNNbak` / `*.orig.py`。

★★ 为什么必须**同时改生成者**（批次10 核心教训「移文件只是一半」）：
  本批次清掉的 `.pyc` 是 **python 自己写的字节码缓存**。`patches/` 下 6 个脚本 import 兄弟模块
  （d2_roundtrip / l2p_l2pdemo / l2p_rportfwd6 / l2p_rportfwd_e2e / l2p_uafix / l2p_bakdir），
  `tools/` 下 20+ 个 import `e2e`。⇒ 光删不清，下次跑判据套件 `patches/__pycache__` 必然长回来。
  ⇒ E1–E9 给 **patches/ 的 6 个 importer + l2p_deploy.sh + l2p_bakdir.py** 加
    `sys.dont_write_bytecode = True` / `export PYTHONDONTWRITEBYTECODE=1`，
    并把**子/孙进程**的字节码用 `PYTHONPYCACHEPREFIX` 重定向到树外（E9 —— 实测 `-B` 不够）。
  ★ tools/ 侧的再生路径**本批次不改**：`run_regress.sh` / `e2e.py` / `deploy.sh` / `build_bpf.sh`
    是冻结的回归 harness（改动会让绿基线失效）⇒ 登记在文件末「残余再生路径」，
    且 `apply` **幂等**：跑完 harness 再 `apply` 一次即可复原。

★ 有意保留（allow-list，**不是**杂物）：
  · `tools/verification_report_*.log` —— `gate4.sh:9` 的产出路径，P4 文档指向它；
    且 gate4 用「日期+时刻」命名 ⇒ 再跑不会覆盖，只会新增。
  · `src_agent/agent/main.c.p4bak` —— **已交付判据的对照输入**（★ 不是回滚点）：
    `tools/p17_no_artifact_verify.py:41-43` 的 `BAK_SRC`，T1 阳性对照用它生成"修前载荷"，
    以证明该判据**能红**。移走 = 打断已交付判据 ⇒ 随交付树走（判据 N13 钉住）。
  · 三个 `.scratch/` 目录**本身**：里面是活脚本/源件（`p83.py` = 批次10 E13 的消费者、
    `p78_fixes.py`、`p95_hoist_write.py`、`p26.py`、`p29_harden.py`、`run84*.sh`、
    `evacuate.c`、`ld_preload_install.c`、`p87_*.txt` …）⇒ 判据 N7 专门钉住"没清过头"。
  · 交付树内 `.bak*`（批次10 范围）与**运行态** `/root/cc/run`（本脚本只扫 `$LST`）。
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True          # 本脚本自己的信条：不在交付树里留 .pyc

LST = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak"
RETIRED = "/root/gp5probe/retired"
LEDGER = os.path.join(BAKDIR, "_clutter_moved.txt")
SUF = ".bak-clutter"
PAT = os.path.join(LST, "extenders/gopher_listener_tcp/patches")

OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"
SKIP_DIRS = ("third_party",)            # 本地 replace 副本，不是我们的产物

# allow-list：有意保留（正则，相对 LST）
KEEP = [
    r"^extenders/linux_agent/tools/verification_report_\d+\.log$",
    # ★ 这不是杂物，是**已交付判据的对照输入**（不是回滚点）：tools/p17_no_artifact_verify.py:41-43
    #   的 BAK_SRC —— T1 阳性对照用"修前 main.c"生成的载荷**必须**产出 /tmp/ax_dbg，
    #   以证明该判据"能红"。移走它 = 打断已交付判据 ⇒ **随交付树走**。
    #   实测（本次）：全树 grep 仅 p17 这一处活引用；树外 /root/gp5probe 零引用。
    r"^extenders/linux_agent/src_agent/agent/main\.c\.p4bak$",
]

# 对照输入（必须随树交付；判据 N13）
MUST_KEEP_INPUT = [
    "extenders/linux_agent/src_agent/agent/main.c.p4bak",
]

# 清扫规则：机器普查，不手工转录
RULES = [
    ("__pycache__/ 目录", "d", lambda r: r.rsplit("/", 1)[-1] == "__pycache__"),
    ("*.pyc",             "f", lambda r: r.endswith(".pyc")),
    ("*.log",             "f", lambda r: r.endswith(".log")),
    ("*.pNNbak 历史备份",  "f", lambda r: re.search(r"\.p\d+bak$", r) is not None),
    ("*.orig.py",         "f", lambda r: r.endswith(".orig.py")),
]

# 生成者编辑（E1–E9，共 10 条）：(文件, 锚点, 替换, 已应用探针)
#   ★ 探针必须**逐条唯一**：三条 E8 曾共用探针 `[py, "-B"` ⇒ E8a 先落地后，
#     E8b/E8c 被判成"探针在锚点前"而**漏应用**（同族坑22 的第 3 种形态）。
EDITS = [
    (os.path.join(PAT, "l2p_deploy.sh"),
     "set -uo pipefail\n",
     "set -uo pipefail\n"
     "# ★ 批次13：判据套件**不许在交付树里写 __pycache__**（python 字节码缓存是\n"
     "#   \"清完长回来\"的唯一真生成者）。本行覆盖本脚本派生的全部 python：\n"
     "#   l2p_depcheck.py / l2p_crypto.py / l2p_e2e.py。\n"
     "export PYTHONDONTWRITEBYTECODE=1\n",
     "export PYTHONDONTWRITEBYTECODE=1"),

    (os.path.join(PAT, "d2_roundtrip.py"),
     "import sys\nimport threading\nimport time\n",
     "import sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：本文件 import l2p_e2e，不许在交付树里写 __pycache__\n"
     "import threading\nimport time\n",
     "sys.dont_write_bytecode = True"),

    (os.path.join(PAT, "l2p_l2pdemo.py"),
     "import sys\n\nsys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n",
     "import sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：import l2p_e2e 前先关掉字节码缓存\n"
     "\nsys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n",
     "sys.dont_write_bytecode = True"),

    (os.path.join(PAT, "l2p_rportfwd6.py"),
     "import os\nimport sys\n\nsys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n",
     "import os\nimport sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：import l2p_rportfwd4 前先关掉字节码缓存\n"
     "\nsys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n",
     "sys.dont_write_bytecode = True"),

    (os.path.join(PAT, "l2p_rportfwd_e2e.py"),
     "import sys\nimport time\n\nHERE = os.path.dirname(os.path.abspath(__file__))\n",
     "import sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：import l2p_e2e 前先关掉字节码缓存\n"
     "import time\n\nHERE = os.path.dirname(os.path.abspath(__file__))\n",
     "sys.dont_write_bytecode = True"),

    (os.path.join(PAT, "l2p_uafix.py"),
     "import subprocess\nimport sys\n",
     "import subprocess\nimport sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：本文件会 import l2p_relay_e2e\n",
     "sys.dont_write_bytecode = True"),

    (os.path.join(PAT, "l2p_bakdir.py"),
     "import subprocess\nimport sys\n\nLST = \"/root/cc/AdaptixServer\"\n",
     "import subprocess\nimport sys\n"
     "sys.dont_write_bytecode = True   # ★ 批次13：W5 的 importlib 动态 import 会写 __pycache__\n"
     "\nLST = \"/root/cc/AdaptixServer\"\n",
     "sys.dont_write_bytecode = True"),

    # ★★★ E8：**子进程**必须显式 `-B` —— 进程内的 `dont_write_bytecode` **传不下去**。
    #   实测：E7 之后复跑 `l2p_bakdir.py verify`，其 W6a 仍给**树内** `tools/.scratch/p83.py`
    #   写出了 `__pycache__`（05:59:11）⇒ 判据自己弄脏被测对象（"移文件只是一半"的第 3 层：
    #   生成者不在 import 处，而在**判据自己 spawn 的子进程**里）。
    (os.path.join(PAT, "l2p_bakdir.py"),
     '    py = sys.executable or "/usr/bin/python3"\n'
     '    for f in CONSUMERS:\n'
     '        cp = subprocess.run([py, "-m", "py_compile", f], capture_output=True, text=True)\n',
     '    # ★ 批次13 E8：子进程是全新解释器 ⇒ 必须显式 `-B`（否则 W6a 给树内 p83.py 写 __pycache__）\n'
     '    py = sys.executable or "/usr/bin/python3"\n'
     '    for f in CONSUMERS:\n'
     '        cp = subprocess.run([py, "-B", "-m", "py_compile", f], capture_output=True, text=True)\n',
     '[py, "-B", "-m", "py_compile", f]'),

    (os.path.join(PAT, "l2p_bakdir.py"),
     '        cp = subprocess.run([py, f] + args, capture_output=True, text=True)\n',
     '        cp = subprocess.run([py, "-B", f] + args, capture_output=True, text=True)\n',
     '[py, "-B", f] + args'),

    (os.path.join(PAT, "l2p_bakdir.py"),
     '    cp = subprocess.run([py, P_L2PLIST, LSTN, "check"], capture_output=True, text=True)\n',
     '    cp = subprocess.run([py, "-B", P_L2PLIST, LSTN, "check"], capture_output=True, text=True)\n',
     '[py, "-B", P_L2PLIST, LSTN, "check"]'),

    # ★★★ E9：**`-B` 不够** —— 实测物证：E8 三处 `-B` 已在位，06:05 复跑 verify 仍写出
    #   `tools/.scratch/__pycache__/p83.cpython-312.pyc`。原因：`-B` / `sys.dont_write_bytecode`
    #   只作用于 **import 缓存**（`SourceLoader.get_code`），而 W6a 用的是 **`py_compile`** ——
    #   它是**显式编译 API，不查**该标志（只认 `sys.pycache_prefix`）。
    #   ⇒ 正解：把 `PYTHONPYCACHEPREFIX` 放进 **os.environ**，由**子/孙进程自动继承**（覆盖任意深度）。
    (os.path.join(PAT, "l2p_bakdir.py"),
     "sys.dont_write_bytecode = True   # ★ 批次13：W5 的 importlib 动态 import 会写 __pycache__\n",
     "sys.dont_write_bytecode = True   # ★ 批次13：W5 的 importlib 动态 import 会写 __pycache__\n"
     "# ★ 批次13 E9：**子/孙进程**的字节码一律落到**树外**（E8 的 `-B` 不够，见 doc/批次13 §十一）。\n"
     "#   环境变量可被所有后代继承 ⇒ 覆盖 W6a 的 py_compile 与任何更深层的 python 调用。\n"
     'os.environ["PYTHONPYCACHEPREFIX"] = "/root/gp5probe/tmp/pycache"\n'
     'os.environ["PYTHONDONTWRITEBYTECODE"] = "1"\n',
     'os.environ["PYTHONPYCACHEPREFIX"]'),
]

# N7：必须仍在树内的活件（清过头的防线）
MUST_KEEP = [
    "extenders/linux_agent/tools/.scratch/p83.py",
    "extenders/linux_agent/tools/.scratch/p78_fixes.py",
    "extenders/linux_agent/tools/.scratch/p95_hoist_write.py",
    "extenders/linux_agent/tools/.scratch/p26.py",
    "extenders/linux_agent/tools/.scratch/p29_harden.py",
    "extenders/linux_agent/tools/.scratch/run84.sh",
    "extenders/linux_agent/tools/.scratch/gap7_watchdog.sh",
    "extenders/linux_agent/.scratch/evacuate.c",
    "extenders/linux_agent/.scratch/ld_preload_install.c",
    "extenders/linux_agent/.scratch/p20_arg_channel_verify.py",
]


# ── 基础设施 ────────────────────────────────────────────────────────────────
def read(p):
    with open(p, "r", encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def write(p, t):
    with open(p, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(t)


def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def dstname(rel):
    """rel → RETIRED 内的目标名（1:1 可逆；双点来自 `.scratch` 的前导点）。
    ★ 唯一性由 N10 断言；ledger 才是权威映射（批次9d 坑13「回滚点串名」的正面防线）。"""
    return rel.replace("/", ".") + SUF


def kept(rel):
    return any(re.match(p, rel) for p in KEEP)


def in_pycache(rel):
    return "__pycache__" in rel.split("/")


def census():
    """活体普查：当前树内命中规则者（已移走的不在此列）。"""
    out = []
    for root, dirs, files in os.walk(LST):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for d in dirs:
            rel = os.path.relpath(os.path.join(root, d), LST)
            if not kept(rel) and RULES[0][2](rel):
                out.append((rel, "d"))
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), LST)
            if kept(rel) or in_pycache(rel):
                continue
            for _n, k, pred in RULES[1:]:
                if k == "f" and pred(rel):
                    out.append((rel, "f"))
                    break
    return sorted(set(out))


def ledger_rows():
    if not os.path.exists(LEDGER):
        return []
    rows = []
    for ln in read(LEDGER).splitlines():
        if not ln.strip() or ln.startswith("#"):
            continue
        parts = ln.split("\t")
        if len(parts) == 3:
            rows.append(tuple(parts))
    return rows


def existing_map():
    """rel → 已登记的 dst 列表（用于**再生成**时的唯一命名）"""
    m = {}
    for _md, rel, dst in ledger_rows():
        m.setdefault(rel, []).append(dst)
    return m


def target_name(rel, em):
    """★ 再生成（rel 已在 ledger 里）⇒ 加 `.regrow-N` 后缀：目标名唯一、**绝不覆盖旧回滚点**。
    实测出处：批次13 首次清完后复跑 `l2p_bakdir.py verify`，其 W6a 的 `py_compile`
    **子进程**又把 `tools/.scratch/__pycache__` 写了一次（见 E8）—— 这正是本规则的第一个使用者。"""
    prev = em.get(rel, [])
    return dstname(rel) if not prev else dstname(rel) + ".regrow-%d" % len(prev)


def edit_state(f, anchor, probe):
    """三态。两种已应用形态都要认：
       ①「锚点被替换」—— 锚点消失、探针出现（E2–E7：探针插在 import 之间 ⇒ 原锚点不再连续）
       ②「锚点后插入」—— 锚点存活、探针出现在**锚点之后**（E1：`set -uo pipefail` 后加 export）
       ★ 只认"锚点被替换"会把 E1 误判成异常（本次 apply 后实测：锚点=1 探针=1）。"""
    if not os.path.exists(f):
        return "★缺文件"
    t = read(f)
    has_a, has_p = t.count(anchor), t.count(probe)
    if has_p >= 1 and has_a == 0:
        return "已应用"
    if has_p == 0 and has_a == 1:
        return "待应用"
    if has_p >= 1 and has_a == 1:
        return "已应用" if t.find(probe) > t.find(anchor) else "★异常(探针在锚点前)"
    return "★异常(锚点=%d 探针=%d)" % (has_a, has_p)


# ── 子命令 ─────────────────────────────────────────────────────────────────
def do_status():
    pend = census()
    rows = ledger_rows()
    print("== 活体普查（待移）: %d 件 ==" % len(pend))
    for rel, kind in pend:
        print("  待移  %-4s %s" % (kind, rel))
    print("== ledger（已移）: %d 件 ==" % len(rows))
    bad = 0
    for m, rel, dst in rows:
        src, d = os.path.join(LST, rel), os.path.join(RETIRED, dst)
        se, de = os.path.exists(src), os.path.exists(d)
        if se and de:
            print("  ★两份并存 %s" % rel); bad += 1
        elif se:
            print("  ★仍在树内 %s" % rel); bad += 1
        elif not de:
            print("  ★缺失     %s" % rel); bad += 1
    print("  已就位 %d / 异常 %d" % (len(rows) - bad, bad))
    print("== 生成者（E1–E9 / %d 条）==" % len(EDITS))
    for f, a, r, pr in EDITS:
        print("  %-9s %s" % (edit_state(f, a, pr), os.path.basename(f)))
    print("== allow-list（有意保留）==")
    for pat in KEEP:
        n = sum(1 for rel, _k in [(r, k) for r, k in _all_files()] if re.match(pat, rel))
        print("  %s 命中 %d" % (pat, n))
    return 0


def _all_files():
    out = []
    for root, dirs, files in os.walk(LST):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            out.append((os.path.relpath(os.path.join(root, f), LST), "f"))
    return out


def do_check():
    r = []
    pend = census()
    em = existing_map()
    r.append((True, "C1 活体普查：%d 件待移（0 件 ⇒ 本次只改生成者，不动文件）" % len(pend)))
    names = [target_name(rel, em) for rel, _k in pend] + [d for _m, _r, d in ledger_rows()]
    r.append((len(names) == len(set(names)), "C2 目标名互不相同（%d 个；含再生成 .regrow-N 命名）" % len(names)))
    r.append((os.path.isdir(RETIRED), "C3 RETIRED 目录存在 %s" % RETIRED))
    for f, a, _rep, pr in EDITS:
        st = edit_state(f, a, pr)
        r.append((st in ("待应用", "已应用"), "C4 %s：%s" % (os.path.basename(f), st)))
    miss = [rel for rel in MUST_KEEP if not os.path.exists(os.path.join(LST, rel))]
    r.append((not miss, "C5 十件活件都在树内（缺 %d）" % len(miss)))
    ok = True
    for good, msg in r:
        print("  %s %s" % (OK if good else BAD, msg))
        ok &= good
    print("RESULT: %s（%d 条，%d 红）" % ("PASS" if ok else "FAIL", len(r), sum(1 for g, _ in r if not g)))
    return 0 if ok else 1


def do_apply():
    rc = do_check()
    if rc != 0:
        print("★ 前置断言未过 ⇒ 不写盘")
        return rc
    pend = census()
    if not pend:
        print("  %s 已是干净态（幂等：无操作）" % INFO)
    # ② 备份生成者 + 前置 md5
    os.makedirs(BAKDIR, exist_ok=True)
    for f, a, _rep, pr in EDITS:
        bak = os.path.join(BAKDIR, os.path.basename(f) + SUF)
        if not os.path.exists(bak):
            shutil.copy2(f, bak)
            print("  %s 生成者 pre-image → %s" % (OK, os.path.basename(bak)))
    # ledger（移动前 md5；目录记 DIR:<文件数>）★ 再生成 ⇒ `.regrow-N` 后缀，绝不覆盖旧回滚点
    em = existing_map()
    new_rows, regrow = [], []
    for rel, kind in pend:
        src = os.path.join(LST, rel)
        dst = target_name(rel, em)
        if em.get(rel):
            regrow.append(rel)
        if kind == "d":
            n = sum(len(fs) for _r, _d, fs in os.walk(src))
            new_rows.append(("DIR:%d" % n, rel, dst))
        else:
            new_rows.append((md5(src), rel, dst))
    if regrow:
        print("  %s ★ 检测到**再生成** %d 件（已在 ledger 中）⇒ 目标名加 .regrow-N：%s"
              % (INFO, len(regrow), regrow[:3]))
    with open(LEDGER, "a", encoding="utf-8") as fh:
        for m, rel, dst in new_rows:
            fh.write("%s\t%s\t%s\n" % (m, rel, dst))
    # ③ 具名 mv（**绝不 rm / find -delete**：无 git ⇒ 一律只移不删）
    for _m, rel, dst in new_rows:
        os.makedirs(RETIRED, exist_ok=True)
        shutil.move(os.path.join(LST, rel), os.path.join(RETIRED, dst))
    print("  %s 具名 mv %d 件 → RETIRED（ledger %d 行）" % (OK, len(new_rows), len(new_rows)))
    # ④ 改生成者
    n = 0
    for f, a, rep, pr in EDITS:
        if edit_state(f, a, pr) == "待应用":
            t = read(f)
            write(f, t.replace(a, rep, 1))
            n += 1
    print("  %s 生成者编辑 %d 处（EDITS 共 %d 条）" % (OK, n, len(EDITS)))
    return do_verify()


def do_revert():
    # 生成者还原
    n = 0
    for f, _a, _rep, _pr in EDITS:
        bak = os.path.join(BAKDIR, os.path.basename(f) + SUF)
        if os.path.exists(bak):
            shutil.copy2(bak, f)
            n += 1
    print("  %s 生成者还原 %d 个" % (OK, n))
    # 按 ledger 具名搬回
    m = 0
    for _md, rel, dst in ledger_rows():
        src, d = os.path.join(LST, rel), os.path.join(RETIRED, dst)
        if os.path.exists(d):
            os.makedirs(os.path.dirname(src), exist_ok=True)
            shutil.move(d, src)
            m += 1
    print("  %s 搬回 %d 件（ledger 仍在：%s；要清空请手工具名处理）" % (OK, m, LEDGER))
    return 0


def do_verify():
    r = []
    pend = census()
    r.append((len(pend) == 0, "N1 交付树（除 third_party）活体异物 = 0（实测 %d）" % len(pend)))
    pyc = [rel for rel, _k in _all_files() if rel.endswith(".pyc")]
    r.append((len(pyc) == 0, "N2 *.pyc = 0（实测 %d）" % len(pyc)))
    logs = [rel for rel, _k in _all_files() if rel.endswith(".log")]
    keepn = [rel for rel in logs if kept(rel)]
    r.append((len(logs) == len(keepn) == 1, "N3 *.log 只剩 allow-list 且**真实存在**（日志 %d / 白名单命中 %d）"
              % (len(logs), len(keepn))))
    r.append((all(re.search(r"verification_report_\d+\.log$", k) for k in keepn),
              "N3b 留下的确是 gate4 报告：%s" % (keepn or "无")))
    pnb = [rel for rel, _k in _all_files() if re.search(r"\.p\d+bak$", rel) and not kept(rel)]
    r.append((len(pnb) == 0, "N4 *.pNNbak（除 allow-list 的对照输入）= 0（实测 %d %s）" % (len(pnb), pnb[:3])))
    org = [rel for rel, _k in _all_files() if rel.endswith(".orig.py") and not kept(rel)]
    r.append((len(org) == 0, "N5 *.orig.py = 0（实测 %d）" % len(org)))
    bad, rows = 0, ledger_rows()
    for m, rel, dst in rows:
        src, d = os.path.join(LST, rel), os.path.join(RETIRED, dst)
        if os.path.exists(src) or not os.path.exists(d):
            bad += 1
        elif m.startswith("DIR:"):
            n = sum(len(fs) for _r, _d, fs in os.walk(d))
            bad += 0 if n == int(m[4:]) else 1
        elif md5(d) != m:
            bad += 1
    r.append((bad == 0 and len(rows) > 0, "N6 ledger %d 行：字节一致 + 原路径已清空 + 无两份并存（异常 %d）" % (len(rows), bad)))
    miss = [rel for rel in MUST_KEEP if not os.path.exists(os.path.join(LST, rel))]
    r.append((not miss, "N7 十件活件仍在树内（没清过头；缺 %s）" % (miss or "无")))
    st = [(os.path.basename(f), edit_state(f, a, pr)) for f, a, _rep, pr in EDITS]
    r.append((all(s in ("待应用", "已应用") for _b, s in st), "N8 %d 处生成者三态可辨：%s" % (len(st), st)))
    r.append((all(s == "已应用" for _b, s in st), "N8b %d 处**已应用**（干净态必须是已应用）" % len(st)))
    n = 0
    # ★ 让 py_compile 的字节码落到**树外**（否则判据自己会在交付树里写 __pycache__ ⇒ 自伤）。
    env = dict(os.environ, PYTHONPYCACHEPREFIX="/root/gp5probe/tmp/pycache")
    uniq = sorted({f for f, _a, _r, _p in EDITS})          # 去重（同一文件可能有多处编辑）
    for f in uniq:
        if f.endswith(".py"):
            p = subprocess.run([sys.executable, "-m", "py_compile", f], capture_output=True, env=env)
            n += 0 if p.returncode == 0 else 1
        else:
            p = subprocess.run(["bash", "-n", f], capture_output=True)
            n += 0 if p.returncode == 0 else 1
    r.append((n == 0, "N9 %d 个生成者语法自检（py_compile / bash -n，按文件去重）red=%d" % (len(uniq), n)))
    emv = existing_map()
    r.append((len(set(target_name(rel, emv) for rel, _k in census())) == len(census()),
              "N10 目标名互不相同（含再生成命名）"))
    n2 = sum(1 for _m, rel, _d in rows if kept(rel))
    r.append((n2 == 0, "N11 allow-list 文件**不在**移动清单里（命中 %d）" % n2))
    kn = [rel for rel in MUST_KEEP_INPUT if not os.path.exists(os.path.join(LST, rel))]
    r.append((not kn, "N13 对照输入随树交付（p17 T1 的 BAK_SRC；缺 %s）" % (kn or "无")))
    # ★ N12 = **纯断言**：本脚本**零删除**（字节码已由 PYTHONPYCACHEPREFIX 重定向出树）；
    #   若仍有残留则报红交人判断，**不自动删** —— "只移不删"同样约束判据自己。
    left = [rel for rel, _k in _all_files() if rel.endswith(".pyc")]
    r.append((not left, "N12 判据自身无副作用（树内 *.pyc：%d %s）" % (len(left), left[:3])))
    ok = True
    for good, msg in r:
        print("  %s %s" % (OK if good else BAD, msg))
        ok &= good
    print("RESULT: %s（%d 条判据，%d 条红）" % ("PASS" if ok else "FAIL", len(r), sum(1 for g, _ in r if not g)))
    return 0 if ok else 1


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    return {"status": do_status, "check": do_check, "apply": do_apply,
            "revert": do_revert, "verify": do_verify}.get(cmd, do_status)()


if __name__ == "__main__":
    sys.exit(main())

# ── 残余再生路径（登记，不在本脚本处置范围）──────────────────────────────────
#  ① tools/*.sh 里以 heredoc `import e2e` / `import p<NN>` 的驱动脚本
#     （crypto_edge_test.sh / leak_test.sh / multiagent_test.sh / reconnect_storm_test.sh /
#       rotation_test.sh / deploy.sh / gate4.sh …）——跑它们会再生成 tools/__pycache__。
#     ★ 不改它们：`run_regress.sh` / `e2e.py` / `deploy.sh` / `build_bpf.sh` 是**冻结的回归 harness**，
#       改动会让已验收的绿基线失效。⇒ 跑完 harness 后**重跑 `apply`（幂等）**即可复原。
#  ② tools/.scratch/{run84.sh,run84b.sh,gap7_watchdog.sh} 会往树内 `.scratch/` 追加日志；
#     它们是一次性历史脚本，重跑才会写（实测当前无进程在写）。
#  ③ 交付前检查单里加一行：`python3 patches/l2p_clutter.py verify | tail -1`（应 PASS）。
