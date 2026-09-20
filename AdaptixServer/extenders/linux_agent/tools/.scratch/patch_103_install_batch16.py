#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_103_install_batch16.py —— 批次16 装机（交付树 / 上传区 -> 运行态 /root/cc/run）

======================= 与 patch_102 的关系（为什么是 import 而不是复制）=======================
★ 本脚本**不复制** patch_102 的机械，只 import 它并**覆盖 4 个数据面**：
      TAG / BAKDIR / STAGE / ARTIFACTS      （+ 新增 preflight 子命令与 2 条额外判据）
  理由（代码为第一根本）：停→换→起→对账这条链上带着**回滚**这种不可逆动作。
  抄第二份出来迟早与第一份漂移，而漂移的表现恰好就是「回滚还原错了文件」——
  也就是最不能出错的那一步。⇒ 单一份实现 + 参数覆盖。
★ 为支持覆盖，对 patch_102 做了 4 处**纯打印 / 文件名前缀**的参数化（TAG、LOCAL_HINTS、
  revert 收尾句、条数文案），每处断言「命中且仅命中 1 次」后写回，并用它的 `status`
  自检过（batch15 装机状态仍全 OK）。

======================= 本次替换清单（3 件，**不含任何 .so / core**）=======================
  #  run 相对路径                                     old md5（= 回滚点）                  new md5
  1  extenders/gopher_listener_tcp/ax_config.axs       2ca3daf4650720e35fd5d8d11f5fa0f4   51518828052c7e50955558c41a84f393
  2  web/index.html                                    825ea20b9317ef70afb3b16f5065e60e   be2b46dd5413b816d7b357243993cf23
  3  web/assets/index-CJmW6xBx.js                      （不存在，本次新增）                99abd2d7357363431c9b0ac3b6abfd16

★ 不碰任何 .so / core ⇒ **不存在**「新 .so + 旧 core = 必挂」那条路径（批次15 的主风险）。
★ 但**仍必须重启**：extender 的 axs 是 core **启动时**按 extender 目录读入的，不是每次请求现读；
  web 侧 index.html 指向的 js 换了文件名 ⇒ 也随重启一并生效。
★ 旧 js（`web/assets/index-CsbzVo2I.js`）**原地保留、不删**：回滚只需还原 index.html
  （本脚本对 web 只做「移入」，从不删除）。

======================= 回滚点（APPLY 与 REVERT 同批交付）=======================
  APPLY   ：python3 patch_103_install_batch16.py install     # 停 → 换 → 起 → 对账
  REVERT  ：python3 patch_103_install_batch16.py revert      # 从回滚点整批还原
            python3 patch_103_install_batch16.py restart     # 必须再重启
  回滚点落盘目录（写入任何字节**之前**建立）：/root/gp5probe/bak16/run_pre_install/
      具名文件：run__extenders__gopher_listener_tcp__ax_config.axs
                run__web__index.html
      第 3 件是**新增**件 ⇒ 无物可备，revert 的动作是「移入 BAKDIR」而不是删除
      （`.installed` 后缀），所以回滚后 assets 目录里既没有幽灵文件也无从丢失原件。

======================= preflight：为什么需要一条**派生**判据 =======================
★ 动机（批次15 实测漏装）：ARTIFACTS 是**手写清单**，它的覆盖面就是判据的覆盖面。
  批次15 我漏了 `linux_agent/ax_config.axs` ⇒ 清单 6/6 全绿，而运行态**根本没有**该命令
  （用户在 Web UI 上一眼看出来的）。⇒ 凡「交付树有、运行态也有」的同类文件，
  一律**从文件系统派生**应检集合，而不是靠人列。
★ 本批次把它提升为装机**前置门**并在装完后再跑一次：全量对账 `dist/` 顶层 32 件，
  差异必须**逐件归类**（本批次的 1 件 dist 项 / 具名豁免 5 件）；
  出现**集合外**差异 ⇒ 判红，先查清再装。

★★ 那 5 件豁免的来历（实测，不是猜的）：
     dist 侧 mtime = **2026-07-23 16:57**（上游交付日），且与本机镜像
     `AdaptixC2-1.5.5/AdaptixServer/extenders/...` **逐字节相同** ⇒ 是上游原件未动；
     run 侧 mtime = **2026-09-13 11:26**（部署日）⇒ 运行态是部署时构建的更新版。
     ⇒ 对这些 extender，「按 dist 装机」是**降级**。判据不能把 `dist == run` 当普适真理。
★★ 豁免**自我失效**机制：每条豁免都记着 dist 侧应有的日期，实测不符 ⇒ 豁免立即作废并判红。
     否则「豁免表」会退化成一块永久免死金牌 —— 与假绿同族。

======================= 用法 =======================
  python3 patch_103_install_batch16.py status      # 只读：现状 vs 期望（3 件）
  python3 patch_103_install_batch16.py stage       # 只读：上传区还缺什么
  python3 patch_103_install_batch16.py preflight   # 只读：全量派生对账 + 内存闸门（**装机前必跑**）
  python3 patch_103_install_batch16.py verify      # 只读：装机后对账 + 2 条额外判据
  python3 patch_103_install_batch16.py install     # ★ 写：停 → 换 → 起 → 对账 → 额外判据
  python3 patch_103_install_batch16.py revert      # 写：整批还原（之后必须 restart）
  python3 patch_103_install_batch16.py selftest    # 只读：证明日志告警检测器**真能变红**

★ 与 build_bpf.sh / e2e.py / run_regress.sh / deploy.sh **无关**，不触碰。
"""

import glob
import os
import re
import subprocess
import sys
import time

SCRATCH = os.path.dirname(os.path.abspath(__file__))
P102_PATH = os.path.join(SCRATCH, "patch_102_install_batch15.py")


def _load_p102():
    import importlib.util
    spec = importlib.util.spec_from_file_location("p102_install", P102_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


if not os.path.isfile(P102_PATH):
    raise SystemExit("★ 找不到可复用的机械：%s" % P102_PATH)

p102 = _load_p102()

RUN = p102.RUN
DELIVER = p102.DELIVER
PATCHES = DELIVER + "/extenders/gopher_listener_tcp/patches"
AXSCHK = PATCHES + "/axs_ui_check.js"

# ── 覆盖数据面（p102 的函数在**调用时**查它自己模块的全局，所以这样覆盖是生效的）──
p102.TAG = "b16"
p102.BAKDIR = "/root/gp5probe/bak16/run_pre_install"
p102.STAGE = "/root/gp5probe/stage16/web"
p102.LOCAL_HINTS = [
    ("AdaptixWeb/dist/index.html", "be2b46dd5413b816d7b357243993cf23"),
    ("AdaptixWeb/dist/assets/index-CJmW6xBx.js", "99abd2d7357363431c9b0ac3b6abfd16"),
]
p102.ARTIFACTS = [
    ("axs_lst", "extenders/gopher_listener_tcp/ax_config.axs",
     DELIVER + "/extenders/gopher_listener_tcp/dist/ax_config.axs", "d",
     "2ca3daf4650720e35fd5d8d11f5fa0f4", "51518828052c7e50955558c41a84f393"),
    ("web_index", "web/index.html",
     p102.STAGE + "/index.html", "s",
     "825ea20b9317ef70afb3b16f5065e60e", "be2b46dd5413b816d7b357243993cf23"),
    ("web_js", "web/assets/index-CJmW6xBx.js",
     p102.STAGE + "/assets/index-CJmW6xBx.js", "s",
     None, "99abd2d7357363431c9b0ac3b6abfd16"),
    # ── 批次18 并入：D3 的 7 个载荷源（交付清单 §4.2 第 5–11 件）────────────────
    #   ★ 为什么并入本脚本而不是另写一个：复用**已验证的机械**（具名回滚点 / 停 / 换 / 起 /
    #     对账 / 内存闸门），且**仍然只停一次**。
    #   ★ 这 7 件是"下次编译 agent 时才入二进制"的源文件 ⇒ **不改当前 agent 行为**，
    #     故本批**不含任何 .so / core**（无「新 .so + 旧 core = 必挂」那条路径）。
    #   ★ 方向已由批次17 判死：src == dist 7/7 ✓、dist ≠ run 7/7 ★ ⇒ 推 dist 那份进 run。
    ("src_main", "extenders/linux_agent/src_agent/agent/main.c",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/main.c", "d",
     "a71c873f928fed6ede45afb32ef7135a", "7943085e9f56c077592b355f87ac3429"),
    ("src_pivot", "extenders/linux_agent/src_agent/agent/pivot.c",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/pivot.c", "d",
     "1d7bf3dd033debcfaff0f97d3f4e8eec", "5a19887a7453dd782c987072deaa8068"),
    ("src_pivot_h", "extenders/linux_agent/src_agent/agent/pivot.h",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/pivot.h", "d",
     "45a6b556ca0ac5bb844d2acbcd5413c6", "458a71b544aefbe316404e50701ed3e7"),
    ("src_tasks_pivot", "extenders/linux_agent/src_agent/agent/tasks_pivot.c",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/tasks_pivot.c", "d",
     "108cc896b7f973e7f24d224959f7cb50", "98973ae383e8f7d0629a3c2f0759204e"),
    ("src_tasks_pivot_h", "extenders/linux_agent/src_agent/agent/tasks_pivot.h",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/tasks_pivot.h", "d",
     "5c77d0af76842ed0c32210f9dce0ceb2", "7c53098c63dd8dfb18834a268af1a7dc"),
    ("src_types_h", "extenders/linux_agent/src_agent/agent/types.h",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/types.h", "d",
     "3875f5098393bba8b389301cef187e13", "da057e69aa8d931c479b48f4d2ed2e5a"),
    ("src_commander", "extenders/linux_agent/src_agent/agent/commander.c",
     DELIVER + "/extenders/linux_agent/dist/src_agent/agent/commander.c", "d",
     "b8e9475a6bd3ae18442da69e0d5726a3", "0c74c1bd8b15eaf8b43b103c0c08e6f6"),
]

md5 = p102.md5

# ── 具名豁免：dist ↔ run 允许存在差异的历史件 ────────────────────────────────
#   值 = 该件 dist 侧**应有**的 mtime 日期（上游交付日）。实测不符 ⇒ 豁免失效并判红。
#
# ★★ 批次18：**豁免表退休，置空**。
#   批次17 的 D5 实测「6 个过期件 6/6 都是 dist 那侧旧」⇒ 结论是"重建 dist 即可归位，
#   不需要解释为什么 dist 可以不一样"。批次18 已把 21 个 extender 的 dist 全量重建，
#   三层同步判据（l2p_threelayer.py verify）现为 **L2 dist↔run 844 文件 0 失配**。
#   此时再留着这 5 条豁免，它们就从"解释"变成了"**掩盖物**"——正是「台账不许烂成垃圾桶」
#   要防的东西。★ 判据因此**变严**：今后任何 dist ≠ run 都是红，无豁免。
EXEMPT = {}

# 我们**自己改过**的 extender：其 dist 子树参与递归对账（其余只做顶层件对账）。
# ★ 为什么单列：这 4 个的 dist 是**我们重建的**权威件，而其余 extender 的 dist
#   是上游原件（见 EXEMPT 的来历）——两者混在一起对账只会互相掩盖。
OWNED = ["linux_agent", "linux_listener_tcp", "gopher_agent", "gopher_listener_tcp"]

MEM_FLOOR_MB = 500          # 低于此值不装机（本机只有 1965 MB 物理内存）


def _expected_new():
    """本批次「装完应该是」的映射：extender/file -> md5（从 ARTIFACTS 派生，不手写）。"""
    out = {}
    for _label, rel, _src, _kind, _old, new in p102.ARTIFACTS:
        if rel.startswith("extenders/"):
            out[rel[len("extenders/"):]] = new
    return out


def _mem_available_mb():
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) // 1024
    except OSError:
        pass
    return None


def _mem_gate():
    """返回非 0 = 拒绝继续。★ 本机 1965 MB 物理内存，装机要「停→起」，先量再动。"""
    avail = _mem_available_mb()
    if avail is None:
        print("  MemAvailable 读不到 ⇒ 不作断言（不把「没量到」当通过）")
        return 0
    print("  MemAvailable    %d MB（下限 %d MB）" % (avail, MEM_FLOOR_MB))
    if avail < MEM_FLOOR_MB:
        print("★ 可用内存不足 ⇒ 先停掉 rig / relay 等非交付进程再装：")
        print("    rig  : /root/gp5probe/rig_run.sh cycle-stop   （39012）")
        print("    relay: pkill -f axrelay                       （4446）")
        return 1
    return 0


def _tree_diff(dir_a, dir_b):
    """递归比较两棵树。返回 (仅 a 有, 仅 b 有, md5 不同) 三个排序列表（相对路径）。"""
    def snap(root):
        out = {}
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                fp = os.path.join(dirpath, fn)
                out[os.path.relpath(fp, root)] = md5(fp)
        return out
    a, b = snap(dir_a), snap(dir_b)
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    diff = sorted(k for k in set(a) & set(b) if a[k] != b[k])
    return only_a, only_b, diff


def cmd_preflight():
    """只读全量派生对账。**装机前与装机后都可以跑**（装机前应看到本批次那 1 件待换）。"""
    print("=== 批次16 preflight（只读全量派生对账）===")
    bad = 0
    want = _expected_new()

    pairs = []
    for d in sorted(glob.glob(DELIVER + "/extenders/*/dist/*")):
        if not os.path.isfile(d):
            continue
        name = d.split("/extenders/")[1].split("/dist/")[0]
        base = os.path.basename(d)
        pairs.append((name + "/" + base, d, RUN + "/extenders/" + name + "/" + base))

    if not pairs:
        print("★ 一个 dist 顶层件都没扫到 ⇒ 路径写错，本项不作数（fail-closed）")
        return 1

    rows = {"同": 0, "待换": 0, "豁免": 0}
    unexpected = []
    missing = []
    for key, dpath, rpath in pairs:
        a, c = md5(rpath), md5(dpath)
        if a is None:
            missing.append((key, c))
        elif a == c:
            rows["同"] += 1
        elif key in want:
            rows["待换"] += 1
            print("  [待换] %-46s run=%s -> %s" % (key, a or "(无)", want[key]))
        elif key in EXEMPT:
            rows["豁免"] += 1
            print("  [豁免] %-46s run=%s ≠ dist=%s（dist 为上游原件，安装=降级）"
                  % (key, a, c))
        else:
            unexpected.append((key, a, c))
            print("  ★[集合外] %-43s run=%s ≠ dist=%s" % (key, a, c))

    print()
    print("  分布：同字节 %d / 待换 %d / 具名豁免 %d / 交付树有而运行态无 %d / 集合外 %d"
          % (rows["同"], rows["待换"], rows["豁免"], len(missing), len(unexpected)))

    if missing:
        bad += 1
        print("  ★ 以下件交付树有、运行态没有 ⇒ 要么漏装要么路径规则不对：")
        for key, c in missing:
            print("      %-46s dist=%s" % (key, c))
    if unexpected:
        bad += 1
        print("  ★ 出现**集合外**差异 ⇒ 有人动过运行态或交付树，先查清再装机（别硬覆盖）。")

    # ── 豁免自我失效检查：日期不符 ⇒ 豁免作废 ──────────────────────────────
    stale = []
    for key, day in sorted(EXEMPT.items()):
        dpath = DELIVER + "/extenders/" + key.split("/")[0] + "/dist/" + key.split("/", 1)[1]
        if not os.path.exists(dpath):
            stale.append((key, day, "(dist 件不存在)"))
            continue
        got = time.strftime("%Y-%m-%d", time.localtime(os.path.getmtime(dpath)))
        if got != day:
            stale.append((key, day, got))
    if stale:
        bad += 1
        print("  ★ 豁免失效 ⇒ 该差异**不再**能用「上游原件」解释，必须逐件重判：")
        for key, day, got in stale:
            print("      %-46s 期望 dist mtime %s，实测 %s" % (key, day, got))
    else:
        print("  豁免自证        OK（%d 条豁免的 dist 侧 mtime 全部 = 上游交付日）" % len(EXEMPT))

    # ── 我们自己改过的 extender：dist 子树递归对账（顶层件之外的覆盖面）──────
    #
    # ★★ 批次19 修：本条判据原先把三类**早已具名**的差异也算成红（52 = 43+1+1+7），
    #   于是每次 preflight 必然红 ⇒ 一条永远红的判据等于没有判据（人会对它脱敏）。
    #   与 `l2p_threelayer.py` 的 EXPECTED_RUN_ONLY / EXPECTED_DIFFERS **同源同理由**：
    #     · run 独有 `*.bak*`  → 运行态回滚点（唯一回滚手段，设计上不进 dist）
    #     · run 独有 `bofs/*.o` → BOF 现场编译产物（from src_agent/bofs/*.c，不进 dist）
    #     · 两侧都有但不同 `src_gopher/config.go` → 运行期生成器回写（pl_main.go:635 随机 key）
    #     · 本批 ARTIFACTS 的**目标件** → 正是要被替换的面，不同才对
    #   ★ 非空洞性自证：两侧文件数必须都 > 0，且逐规则打印命中数、最后打印**解释后残余**。
    #     残余 != 0 才是红 —— 判据变严了（原先"任何差异皆红"会被人无视，现在"无法解释的差异才红"）。
    print()
    print("  自有 extender 递归对账（dist 子树 ↔ 运行态子树）：")
    # 本批替换面：ARTIFACTS 里落在 extender 树内的目标件（run 相对路径 → extender 内相对路径）
    targets = set()
    for _a in p102.ARTIFACTS:
        _rel = _a[1]
        if _rel.startswith("extenders/"):
            _parts = _rel.split("/", 2)
            if len(_parts) == 3 and _parts[1] in OWNED:
                targets.add((_parts[1], _parts[2]))

    def _explain(ext_name, rel):
        """返回 (理由, 是否本批替换面)；无解释 ⇒ (None, False)。理由均指得出出处。"""
        if (ext_name, rel) in targets:
            return "本批替换目标（ARTIFACTS）", True
        if re.search(r"\.bak(-|$)", rel):
            return "运行态回滚点（*.bak*，设计上不进 dist）", False
        if re.match(r"^bofs/[^/]+\.o$", rel):
            return "BOF 现场编译产物（源在 src_agent/bofs/*.c，不进 dist）", False
        if ext_name + "/" + rel == "gopher_agent/src_gopher/config.go":
            return "运行期生成器回写（pl_main.go:635 随机 key，dist=模板态/run=生成态）", False
        return None, False

    tot = 0          # 解释后残余（真红）
    explained = {}   # 理由 → 命中数
    n_replaced = 0   # 本批替换面命中数
    nonvac = True
    for name in OWNED:
        da = DELIVER + "/extenders/" + name + "/dist"
        db = RUN + "/extenders/" + name
        for _d, _tag in ((da, "dist"), (db, "run")):
            if not os.path.isdir(_d):
                print("    %-20s ★ %s 目录不存在 ⇒ 不作数" % (name, _tag))
                bad += 1
                nonvac = False
                break
        else:
            na = sum(len(f) for _, _, f in os.walk(da))
            nb = sum(len(f) for _, _, f in os.walk(db))
            if na == 0 or nb == 0:
                print("    %-20s ★ 空树（dist %d 文件 / run %d 文件）⇒ 判据空洞，判红" % (name, na, nb))
                bad += 1
                nonvac = False
                continue
            only_a, only_b, diff = _tree_diff(da, db)
            print("    %-20s dist %d 文件 / run %d 文件 ⇒ 仅dist %d / 仅run %d / md5 不同 %d"
                  % (name, na, nb, len(only_a), len(only_b), len(diff)))
            left = []
            for tag, lst in (("仅dist", only_a), ("仅run", only_b), ("不同", diff)):
                for rel in lst:
                    why, is_tgt = _explain(name, rel)
                    if why is None:
                        left.append((tag, rel))
                    elif is_tgt:
                        n_replaced += 1
                    else:
                        explained[why] = explained.get(why, 0) + 1
            tot += len(left)
            for tag, rel in left[:8]:
                print("        ★残余 %-6s %s" % (tag, rel))
            if len(left) > 8:
                print("        ★残余 …（共 %d 条）" % len(left))
    if explained or n_replaced:
        print("    已解释（具名，非掩盖）：")
        for why, n in sorted(explained.items(), key=lambda kv: -kv[1]):
            print("        %-3d × %s" % (n, why))
        if n_replaced:
            print("        %-3d × 本批替换目标（ARTIFACTS，装机后应归零）" % n_replaced)
    if not nonvac:
        print("  ★ 非空洞性检查未通过 ⇒ 本判据本次不产生有效结论")
    if tot:
        bad += 1
        print("  ★ 自有 extender 子树 %d 处**无法解释**的差异 ⇒ 这不是本批次的替换面，先查清"
              "（可能是源码改了但 dist 未重出，或运行态被人改过）" % tot)
    elif nonvac:
        print("    解释后残余 0 处 ⇒ 自有 extender 的 dist 子树与运行态之间没有无法解释的差异")

    print()
    print("结论：%s" % ("preflight 全绿 —— 集合外零差异，可以装机。" if not bad
                     else "preflight 有 %d 项红 ⇒ **先别装**。" % bad))
    return 0 if not bad else 3


def extra_judges():
    """装机件的**语义**判据（md5 相等只证明「是那一份」，不证明「那一份是对的」）。"""
    bad = 0

    # J1：拿 UI 判据脚本评**已装机的那一份** axs —— 不是源码、不是 dist，就是运行态那件。
    axs = RUN + "/extenders/gopher_listener_tcp/ax_config.axs"
    if not os.path.isfile(AXSCHK):
        print("  J1 装机 axs 语义  ★判据脚本不在：%s" % AXSCHK)
        bad += 1
    else:
        r = subprocess.run(["node", AXSCHK, axs], capture_output=True, text=True)
        lines = [l for l in (r.stdout or "").splitlines() if l.strip()]
        ok = (r.returncode == 0)
        print("  J1 装机 axs 语义  %s（%s on %s）"
              % ("OK" if ok else "★红", os.path.basename(AXSCHK), axs))
        print("      %s" % (lines[-1].strip() if lines else "(无输出)"))
        if not ok:
            bad += 1
            for l in lines[-14:]:
                print("      %s" % l)
            if r.stderr:
                print("      stderr: %s" % r.stderr.strip()[:300])

    # J2：发出去的那份 js **正文**里确实带着 libp2p 面。
    #   ★ 传递性说明（可追溯，不是"顺手也查一下"）：
    #     p102.cmd_verify 的「服务端发 web」判据已证明 md5(服务端真发出来的 js) == md5(交付件)；
    #     本条证明 md5(交付件) 的正文里有这三个标记 ⇒ 合起来 = 服务端发出的字节带功能。
    #     所以这里读 run 树上的文件即可，无需再发一次 HTTPS（且省一次 400 KB 传输）。
    jsrel = [rel for _l, rel, _s, _k, _o, _n in p102.ARTIFACTS if rel.startswith("web/assets/")]
    if not jsrel:
        print("  J2 载荷面标记    ★ARTIFACTS 里没有 web/assets/* ⇒ 判据本身没覆盖面")
        bad += 1
    else:
        rel = jsrel[0]
        try:
            with open(os.path.join(RUN, rel), "rb") as f:
                blob = f.read()
        except OSError as e:
            print("  J2 载荷面标记    ★读不到 %s（%s）" % (rel, e))
            blob = None
        if blob is not None:
            marks = [("l2p_relays", b"l2p_relays"),
                     ("p2p-circuit", b"p2p-circuit"),
                     ("传输协议", "传输协议".encode("utf-8"))]
            counts = [(n, blob.count(p)) for n, p in marks]
            miss = [n for n, c in counts if c < 1]
            print("  J2 载荷面标记    %s（%s：%s）"
                  % ("OK" if not miss else "★缺 %s" % miss, rel,
                     " / ".join("%s×%d" % (n, c) for n, c in counts)))
            if miss:
                bad += 1
    return bad


def cmd_verify():
    rc = p102.cmd_verify()
    print()
    print("=== 批次16 额外判据（装机件语义）===")
    bad = extra_judges()
    if bad:
        print("★ 额外判据 %d 条红。" % bad)
        return 9
    print("额外判据全绿（J1 装机 axs 语义 + J2 载荷面标记）。")
    return rc


def cmd_install():
    print("=== 批次16 装机前闸门 ===")
    rc = _mem_gate()
    if rc:
        return rc
    pre = cmd_preflight()
    if pre != 0:
        print("★ preflight 未过 ⇒ 拒绝装机（装机本身是停生产进程的动作，闸门必须在它之前）。")
        return pre
    print()
    rc = p102.cmd_install()
    if rc != 0:
        return rc
    print()
    print("=== 批次16 额外判据（装机件语义）===")
    bad = extra_judges()
    if bad:
        print("★ 额外判据 %d 条红 ⇒ md5 对但**语义**不对，先别用；"
              "必要时 `revert` + `restart`。" % bad)
        return 9
    print("额外判据全绿。")
    print()
    print("★ 装机完成。**浏览器标签页必须硬刷新**：")
    print("    axs 由 core 在启动时读入、经 WS 在登录/订阅时下发 ⇒ 开着的标签页拿不到新命令表；")
    print("    Web 侧 js 换了文件名（Vite 内容哈希）⇒ index.html 指向新件，硬刷新即可。")
    return 0


def cmd_status():
    rc = p102.cmd_status()
    print()
    print("★ 批次16 特有：装机后除 md5 对账外，还要 `verify` 跑 J1/J2 两条**语义**判据。")
    return rc


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    table = {
        "status": cmd_status,
        "stage": p102.cmd_stage,
        "preflight": cmd_preflight,
        "verify": cmd_verify,
        "install": cmd_install,
        "apply": p102.cmd_apply,
        "restart": p102.cmd_restart,
        "revert": p102.cmd_revert,
        "selftest": p102.cmd_selftest,
    }
    if cmd not in table:
        print(__doc__)
        return 1
    return table[cmd]()


if __name__ == "__main__":
    sys.exit(main())
