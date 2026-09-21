#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""l2p_threelayer.py —— 三层同步判据（src → dist → run）  v2

为什么需要它（批次17/18）：
  交付树有三层 —— `src/`（源码）→ `<extender>/dist/`（构建产物）→ `/root/cc/run/extenders/<n>/`（部署副本）。
  **此前没有任何判据在看这三层的同一性**，批次17 一次审计就翻出 D1/D2/D3/D5
  外加「474 个文件只存在于 run」。本脚本把这条补上。

三层各自的判据（分母一律自证，逐个打印名字，fail-closed）：
  L1 src ↔ dist  直拷件（config.yaml / ax_config.axs）三层同 md5。
  L2 dist ↔ run  逐文件同 md5 —— 这是主判据。
  L3 src → dist  编译产物复现（`rebuild` 模式）：把每个插件建到**树外**，三段比 md5。
                 ★ L3 是唯一能发现「dist 与 run 一致但**都落后于源码**」的判据 ——
                   这正是 D1 的形态，纯 L2 看不见。

分类（不分类就只能"要么假红要么被调松"）：
  match / differs / dist_only / run_only
    · differs     落在 EXPECTED_DIFFERS 或 PENDING ⇒ 不算红，各自具名打印
    · run_only    落在 EXPECTED_RUN_ONLY（回滚点 / 现场编译产物）⇒ 不算红
    · 其余一律红

★ 两条防自欺机制：
  1. 每条豁免都必须**具名 + 带理由**，且理由要指得出出处（文件行号 / 交付清单章节）。
  2. PENDING 台账里**已经不再失配**的条目 ⇒ 判红（"请清空"）。
     否则台账会退化成垃圾桶：装上去了还挂着"待装机"，看起来永远有理由。
★ 只读、无副作用（跑完树内 *.pyc 仍为 0 —— `l2p_clutter.py` N12 会复核）。

用法: l2p_threelayer.py [verify|rebuild]
"""
import sys

sys.dont_write_bytecode = True

import hashlib
import os
import re
import shutil
import subprocess
import tempfile

D = "/root/cc/AdaptixServer"
EXT = os.path.join(D, "extenders")
RUN = "/root/cc/run/extenders"
GO = "/usr/local/go/bin/go"

# ── 运行期生成物（设计上**不**进 dist）────────────────────────────────
GEN_PAT = [
    re.compile(r"^src_agent/agent/(config\.h|ApiDefines\.h|strings_obf\.h)$"),
]
# ── run 独有但**预期**（每条具名理由 + 出处）──────────────────────────
EXPECTED_RUN_ONLY = [
    (re.compile(r"\.bak(-|$)"),
     "运行态回滚点：run 树内 .bak* 是唯一回滚手段（交付清单 §8.2「必须留」）"),
    (re.compile(r"^bofs/[^/]+\.o$"),
     "BOF 构建产物：由 tools/compile_check.sh 从 src_agent/bofs/*.c 现场编译，"
     "linux_agent/Makefile 设计上不复制进 dist"),
]
# ── 两边都有但**预期**不同（具名）────────────────────────────────────
EXPECTED_DIFFERS = [
    ("gopher_agent/src_gopher/config.go",
     "运行期生成器回写：pl_main.go:635 用随机 key 重写该文件；"
     "dist 是模板态、run 是生成态 ⇒ 结构性差异，非陈旧"),
    (re.compile(r"^beacon_agent/objects_(dns|http|smb|tcp)/[^/]+\.o$"),
     "nasm 不可复现产物：src_beacon/Makefile 用 `nasm -f win32|win64` 汇编 beacon/asm/*.asm；"
     "实测 251 B 的 .o 两侧**仅第 5/6/7 字节不同**，正是 COFF 头 TimeDateStamp(偏移 4..7) "
     "⇒ nasm 把构建时刻写进目标文件，天然不可逐字节复现，非陈旧"
     "（三个抽样文件差异字节数均 == 3、偏移均 == 5 6 7）"),
]
# ── 待装机台账（交付侧领先，等一次性授权）★ 装机后必须清空 ──────────
# ── ★★ 待装机台账（PENDING）—— 批次19 清空，且**立准入规则** ──────────────────
#
# 为什么现在是空的：批次16 那 10 件已于 2026-09-17 10:08 一次性装机完成
# （`patch_103_install_batch16.py install` RC=0，10/10 md5 复核一致、回滚点
#   `/root/gp5probe/bak16/run_pre_install` 建于写入之前）。装机后本判据的
# L4 **自己把 8 条挂账判红**（"仍失配 0 / 台账 8 ⇒ 请清空"）⇒ 本条按机制要求清空。
#
# ★ 准入规则（不满足就别往这里加，去修真因）：
#   1. 条目必须对应**一个已定稿、已授权、尚未执行的装机动作**；写清"交付件 md5 → 待装机"
#      与"run 现役 md5"，使第三方能独立复核它**确实**还失配。
#   2. 一经装机，**当批必须清空**；L4 会强制这一点（已不再失配 ⇒ 判红）。
#   3. **不是**用来解释"为什么 dist 可以不一样"的：那类差异要么修 dist（对应 EXPECTED_DIFFERS
#      的具名理由），要么是真缺陷。台账只表示"时间差"（交付侧领先、等一次授权），
#      不表示"结构差"。
#   4. 不许写成通配/批量条目 —— 每条一个具名路径，理由指得出出处。
#
# 历史留痕：
#   · 批次18 **撤回**过一条 "gopher_listener_tcp/listener_gopher_tcp.so → 待装机(D1)"：
#     D1 是我自己的探针污染了 dist —— `b18_rebuild_dists.sh` 未带 GOWORK=off，用 go.work
#     生效的配方把干净的 f062bd43… 覆盖成 8549db50…。已从 dist_before.tgz 快照恢复；
#     L4 当场把该条目标红 ⇒ 机制生效（这是本判据最有价值的一次开火）。
PENDING = {}
HEAD = 12


def md5(p):
    h = hashlib.md5()
    try:
        with open(p, "rb") as f:
            for c in iter(lambda: f.read(1 << 20), b""):
                h.update(c)
    except OSError as e:
        return "ERR:%s" % e
    return h.hexdigest()


def walk(root):
    out = {}
    for dp, dns, fns in os.walk(root):
        dns[:] = [d for d in dns if d != ".git"]
        for fn in fns:
            ap = os.path.join(dp, fn)
            if os.path.islink(ap) or not os.path.isfile(ap):
                continue
            out[os.path.relpath(ap, root).replace(os.sep, "/")] = ap
    return out


def extenders():
    return [n for n in sorted(os.listdir(EXT))
            if os.path.isfile(os.path.join(EXT, n, "Makefile"))]


def _m(rel):
    for p, why in EXPECTED_RUN_ONLY:
        if p.search(rel):
            return why
    return None


def verify():
    r = []
    exts = extenders()
    print("[*] 应检 extender 集合由 extendsers/*/Makefile 派生 ⇒ %d 个" % len(exts))
    print("    " + " ".join(exts))
    r.append((len(exts) == 21, "L0 应检 extender 数由被测对象派生且 == 21（实测 %d）" % len(exts)))

    seen_pending = set()
    tot = dict(same=0, diff=0, donly=0, rexp=0, runexp=0)
    bad, pend_hit = [], []
    for e in exts:
        dp, rp = os.path.join(EXT, e, "dist"), os.path.join(RUN, e)
        if not os.path.isdir(dp):
            bad.append("%s（无 dist）" % e)
            continue
        if not os.path.isdir(rp):
            bad.append("%s（run 侧无目录）" % e)
            continue
        dm, rm_ = walk(dp), walk(rp)
        keys = sorted(set(dm) | set(rm_))
        n = dict(same=0, diff=0, donly=0, rexp=0, runexp=0)
        det = []
        for k in keys:
            a, b = dm.get(k), rm_.get(k)
            key = "%s/%s" % (e, k)
            if a and b:
                if md5(a) == md5(b):
                    n["same"] += 1
                else:
                    why = None
                    for nm, w in EXPECTED_DIFFERS:
                        # ★ 允许正则：除具名路径外还有**成类**的预期差异（20 个 nasm .o）
                        if nm == key or (hasattr(nm, "search") and nm.search(key)):
                            why = w
                    if key in PENDING:
                        why = PENDING[key]
                        pend_hit.append(key)
                        seen_pending.add(key)
                    if why:
                        n["diff"] += 1
                        det.append(("pending" if key in PENDING else "expect-diff", k, why))
                    else:
                        n["diff"] += 1
                        det.append(("differs★★", k, "%d vs %d B" % (os.path.getsize(a), os.path.getsize(b))))
                        bad.append(key)
            elif a:
                n["donly"] += 1
                det.append(("dist_only", k, "%d B" % os.path.getsize(a)))
                bad.append(key + "（仅 dist 有）")
            else:
                why = _m(k)
                if why:
                    n["rexp"] += 1
                else:
                    n["runexp"] += 1
                    det.append(("run_only★", k, "%d B" % os.path.getsize(b)))
                    bad.append(key + "（仅 run 有）")
        for kk in n:
            tot[kk] += n[kk]
        flag = "!!" if any(x in ("differs★★", "dist_only", "run_only★") for x, _, _ in det) else "ok"
        print("[%s] %-24s 比对 %4d = 同 %4d / 预期异 %3d / 异★ %3d / dist独有 %3d / run独有 %3d(预期 %d)"
              % (flag, e, len(keys), n["same"],
                 n["diff"] - sum(1 for x, _, _ in det if x == "differs★★"),
                 sum(1 for x, _, _ in det if x == "differs★★"),
                 n["donly"], n["runexp"] + n["rexp"], n["rexp"]))
        for kind, k, info in det[:HEAD]:
            print("      %-11s %-40s %s" % (kind, k, info))
        if len(det) > HEAD:
            print("      ... 还有 %d 条" % (len(det) - HEAD))

    print("[*] 全局：同 %d / 预期异 %d / 异★ %d / dist独有 %d / run独有(预期) %d / run独有★ %d"
          % (tot["same"], tot["diff"], tot["donly"] + tot["runexp"], tot["donly"],
             tot["rexp"], tot["runexp"]))
    r.append((not bad, "L2 dist ↔ run 逐文件同 md5（失配 %d 条：%s）"
              % (len(bad), "; ".join(bad[:8]) + (" …" if len(bad) > 8 else "") if bad else "无")))

    # L1 src ↔ dist 直拷件
    t1bad = []
    for e in exts:
        for f in ("config.yaml", "ax_config.axs"):
            sp = os.path.join(EXT, e, f)
            if not os.path.isfile(sp):
                continue
            dp = os.path.join(EXT, e, "dist", f)
            rp = os.path.join(RUN, e, f)
            ms = md5(sp)
            md = md5(dp) if os.path.isfile(dp) else "缺失"
            mr = md5(rp) if os.path.isfile(rp) else "缺失"
            if not (ms == md == mr):
                if "%s/%s" % (e, f) in PENDING:
                    continue
                t1bad.append("%s/%s src=%s dist=%s run=%s" % (e, f, ms[:8], md[:8], mr[:8]))
    r.append((not t1bad, "L1 src == dist == run 直拷件（%d 条不符：%s）"
              % (len(t1bad), "; ".join(t1bad) if t1bad else "无")))

    # 台账卫生：已挂 PENDING 却不再失配 ⇒ 红
    stale = sorted(set(PENDING) - seen_pending)
    r.append((not stale, "L4 待装机台账无陈旧条目（仍失配 %d / 台账 %d；陈旧：%s）"
              % (len(seen_pending), len(PENDING), ", ".join(stale) if stale else "无")))
    for k in sorted(seen_pending):
        print("      [pending] %-42s %s" % (k, PENDING[k]))
    r.append((len(exts) and (tot["same"] + tot["diff"] + tot["donly"] + tot["rexp"] + tot["runexp"]) > 0,
              "L5 非空洞性：比对文件数 > 0（实测 %d）"
              % (tot["same"] + tot["diff"] + tot["donly"] + tot["rexp"] + tot["runexp"])))

    np_ = sum(1 for ok, _ in r if ok)
    for ok, msg in r:
        print("  [%s] %s" % ("ok  " if ok else "RED ", msg))
    print("RESULT: %s（%d 条判据，%d 条红）" % ("PASS" if np_ == len(r) else "FAIL", len(r), len(r) - np_))
    return 0 if np_ == len(r) else 1


def _extract_cmd(d):
    """★ 让 make 自己展开（`make -n -B <target>`）拿命令行，而不是我用正则扫 Makefile。
    坑30 的教训：正则/手抄抽取会漏 `$(GO) build $(GOFLAGS)` 这类变量形式，漏了还会
    **静默跳过**。`-n` 只打印不执行；`-B` 强制打印 —— 否则产物已存在时 make 会说
    `Nothing to be done for 'plugin'.`（agent_kharon 正是这个形态，实测到）。
    返回含 `-buildmode=plugin` 的那一行（唯一真源）。"""
    try:
        txt = open(os.path.join(d, "Makefile"), encoding="utf-8", errors="replace").read()
    except OSError:
        txt = ""
    tgt = "plugin" if re.search(r"^plugin\s*:", txt, re.M) else "all"
    try:
        r = subprocess.run(["make", "-n", "-B", tgt], cwd=d,
                           capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None, tgt
    # ★ 反斜杠续行必须先拼回逻辑行：make -n 是**逐物理行**回显的，
    #   agent_kharon 的配方写成 `go build $(GOFLAGS) \` / `-o $@ \` / `$(PLUGIN_FILES)`，
    #   `-o` 落在第二行 ⇒ 逐行扫必然漏（同坑30 家族：抽取器只认一种写法）。
    joined = r.stdout.replace("\\\n", " ")
    for ln in joined.splitlines():
        if "-buildmode=plugin" in ln and re.search(r"(^|\s)-o\s+\S+", ln):
            return ln.strip(), tgt
    return None, tgt


def rebuild():
    """L3：src → dist 的**编译产物**复现。把每个插件建到树外，三段比 md5。
    ★ 必须 GOWORK=off：go.work 会对全部 workspace 模块做 MVS，把依赖提版
      （cpuid v2.2.8→v2.3.0 / pprof 提版），产物换字节 —— 与 D4 同机理。
      **本批次据此撤回 D1**：GOWORK=off 重建 gopher_listener_tcp == 运行态 f062bd43…"""
    out = tempfile.mkdtemp(prefix="tl3_", dir="/root/gp5probe")
    rows, bad = [], []
    for e in extenders():
        d = os.path.join(EXT, e)
        mk = os.path.join(d, "dist")
        if not os.path.isdir(mk):
            rows.append((e, "-", "-", "-", "无 dist，跳过"))
            bad.append(e + "（无 dist）")
            continue
        cmd, tgt = _extract_cmd(d)
        if not cmd:
            rows.append((e, "-", "-", "-", "make -n -B %s 抽不到 -buildmode=plugin 行" % tgt))
            bad.append(e + "（命令抽取失败 —— 不静默跳过，判红）")
            continue
        so = os.path.join(out, e + ".so")
        cmd = re.sub(r"(^|\s)-o\s+\S+", r"\1-o " + so, cmd, count=1)
        env = dict(os.environ, GOWORK="off", GOPROXY="off")
        r = subprocess.run(["bash", "-lc", cmd], cwd=d, capture_output=True, text=True, env=env)
        if r.returncode != 0 or not os.path.exists(so):
            rows.append((e, "-", "-", "-", "构建失败：" + (r.stderr.strip().splitlines() or [""])[-1][:60]))
            bad.append(e + "（构建失败）")
            continue
        m3 = md5(so)
        cands = []
        for base in (mk, os.path.join(RUN, e)):
            for fn in os.listdir(base):
                if fn.endswith(".so"):
                    cands.append((base, fn))
        if not cands:
            rows.append((e, m3[:8], "-", "-", "两侧都无 .so 可比"))
            bad.append(e + "（无 .so 可比）")
            continue
        for base, fn in cands:
            m2 = md5(os.path.join(base, fn))
            tag = "dist" if base == mk else "run "
            rows.append((e, m3[:8], m2[:8], "==" if m3 == m2 else "≠", tag + "/" + fn))
            if m3 != m2:
                bad.append("%s: 重建 %s %s %s=%s" % (e, m3[:8], "≠", tag, m2[:8]))
    print("%-24s %-10s %-10s %-3s %s" % ("extender", "重建", "现场", "", "对比对象"))
    for row in rows:
        print("%-24s %-10s %-10s %-3s %s" % row)
    print("[*] 树外构建目录 %s（不写交付树）" % out)
    ok = not bad
    if bad:
        for b in bad:
            print("  [RED ] %s" % b)
    print("RESULT: %s（L3 编译产物三段比对，%d 个 extender，%d 条红）"
          % ("PASS" if ok else "FAIL", len(rows), len(bad)))
    return 0 if ok else 1


if __name__ == "__main__":
    a = sys.argv[1] if len(sys.argv) > 1 else "verify"
    sys.exit(rebuild() if a == "rebuild" else verify())
