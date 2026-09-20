#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p22_wiring_verify.py — 「接线」判据：写出可枚举产物的 BOF 必须自己登记 hide_names

起因 **ISS-071 / ISS-072**：`ko_load.c` 与 `ld_preload_install.c` 都**创建可枚举的
产物**，却都没调 `AxHideNameAdd` —— 于是 `redact.c` 那套「按 hide_names 抹平」**没有
触发条件**（`redact.c:340` 是 `bpf_map_lookup_elem(&hide_names, kb)` 在驱动）：
`cat /proc/modules` 里模块名原样可读、`ls /etc` 里 `ld.so.preload` 原样可数。
`cron_persist` / `systemd_persist` 接了，这两个漏了 —— 同一条不变量，两处漏接。

★ 为什么长期没被发现（比缺陷本身更值钱）：`p15_redact_verify.py:207-208` 在加载
redact 时**自己把模块名当 seed 喂给 `ebpf_load` 的 <names> 参数**：
    names = (MARKER + "\\x00" + MODULES_NAME).encode()
**测试供给了生产路径本该自己供给的那个输入**，于是它测的是「抹平机制有效」，而不是
「生产路径会去登记」。接线缺失被完全掩盖。这与 ISS-060（gap10 T4c 恒真）、
ISS-065（恒真布尔）同族：**判据不能依赖测试自己注入的前提**。

所以本文件的核心不是"再测一遍抹平"，而是**只依赖生产路径自己的行为**：
不手工 `hide_add`、不把产物名喂给 `<names>` —— 只调 `ko_load`，然后断言名字不可读。

  T1  静态接线表：SPEC 里每条 "hide" 的 BOF 源码必须出现 `AxHideNameAdd`
  T2  静态留档：SPEC 里每条 "visible" 的 BOF 必须**不**出现 `AxHideNameAdd`，
      且带 `@visible:<理由>` 标注 —— 有意不藏必须留档，否则下一个人会当 bug 修
  T3  ★ 漂移检测：扫全部 `bofs/*.c`，凡用「创建原语」（O_CREAT/AxFinitModule/
      AxMkdir）却既不在 SPEC 也不在 NO_ARTIFACT 里的 → FAIL。这张表会过期，
      漂移检测是让它在**新增持久化 BOF 时自己报警**，而不是静默放过
  T4  运行时：`ko_load` 加载真模块 → 回执 `hide: <name> -> ok`
  T5  ★ 运行时带内阳性对照：**同一次** `cat /proc/modules` 里，经 ko_load 加载的
      模块名不可读、经 insmod 加载的对照模块名可读 —— 同一次读、同一个 128 字节
      尾部窗口，排除"整窗被抹/观测坏了"这类假阳性
  T6  卸载 eBPF 后该名字重新可读（模块仍加载着）→ 证明是"**藏住了**"不是"没加载上"
  T7  清理：rmmod 两个模块 + 删 pin，`/proc/modules` 与 `/sys/fs/bpf` 归零

用法: p22_wiring_verify.py [--listener TestHTTP] [--static-only]
      `--static-only` 只跑 T1-T3（不需要 agent，也不碰批处理在用的端口/pin）
"""
import os
import re
import subprocess
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                     # noqa: E402
import gap10_rootkit_verify as G               # noqa: E402  复用观测原语与 BOF 打包

BOFS = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bofs"
KO_DIR = "/tmp/p22ko"              # 编译缓存（留在 /tmp，重跑不必重编）
PIN = "axp22"                      # redact 的 pin
PROBE = "axp22probe"               # 经 ko_load 加载 —— 必须被登记
CTRL = "axp22pos"                  # 经 insmod 加载 —— 对照，必须仍然可读

R = []


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-52s %s" % (status, item, note), flush=True)


# ── SPEC：产物 → 期望接线。这不是"文档"，T1/T2 会拿它逐条对源码断言 ──────────
#   "hide"    → 创建可枚举产物 ⇒ 必须调 AxHideNameAdd
#   "visible" → 有意不藏 ⇒ 必须不调，且源码里留 @visible: 理由
SPEC = [
    ("cron_persist.c",       "/etc/cron.d/<job>",           "hide",    ""),
    ("systemd_persist.c",    "/etc/systemd/system/<unit>",  "hide",    ""),
    ("ko_load.c",            "/proc/modules 模块名",          "hide",    ""),
    ("ld_preload_install.c", "/etc/ld.so.preload",          "hide",    ""),
    ("ssh_persist.c",        "~/.ssh/authorized_keys",      "visible",
     "authorized_keys 是极常见文件名，藏了反而让 `ls ~/.ssh` 反常（源码 48-52 行已论证）"),
]

# 用到创建原语但**不需要**登记：产物不是"新增的可枚举物"。豁免必须带理由 ——
# 无理由的豁免就是下一个 ISS-071。
NO_ARTIFACT = {
    "evacuate.c":    "撤离路径：产物是**删**不是增（还原 ld.so.preload 到「本不存在」）",
    "memfd_exec.c":  "匿名 memfd / tmpfs 载体，--unlink 用完即删，磁盘无持久产物",
}

CREATE_RE = re.compile(r"O_CREAT|AxFinitModule|AxMkdir")


def src(fn):
    with open(os.path.join(BOFS, fn), "r", errors="replace") as f:
        return f.read()


def static_checks():
    names = {s[0] for s in SPEC}
    for fn, art, want, why in SPEC:
        try:
            s = src(fn)
        except OSError as e:
            rec("T1/T2 %s" % fn, "FAIL", "读不到源码：%s" % e)
            continue
        wired = "AxHideNameAdd" in s
        if want == "hide":
            rec("T1 %s 登记 %s" % (fn, art), "PASS" if wired else "FAIL",
                "源码%s AxHideNameAdd" % ("含" if wired else "**缺**"))
        else:
            marked = "@visible:" in s
            ok = (not wired) and marked
            rec("T2 %s 有意不藏 %s" % (fn, art), "PASS" if ok else "FAIL",
                "%s AxHideNameAdd；%s @visible: 标注"
                % ("含(不该)" if wired else "不含", "有" if marked else "**缺**"))

    # T3 漂移检测
    drift = []
    for fn in sorted(os.listdir(BOFS)):
        if not fn.endswith(".c"):
            continue
        try:
            s = src(fn)
        except OSError:
            continue
        if not CREATE_RE.search(s):
            continue
        if fn in names or fn in NO_ARTIFACT:
            continue
        drift.append(fn)
    rec("T3 漂移检测：创建原语 BOF 均已归类", "PASS" if not drift else "FAIL",
        "未归类=%s" % (drift or "无"))


# ── 运行时部分 ───────────────────────────────────────────────────────────────
KO_TMPL = """#include <linux/module.h>
#include <linux/kernel.h>
static int __init %(n)s_init(void) { return 0; }
static void __exit %(n)s_exit(void) { }
module_init(%(n)s_init);
module_exit(%(n)s_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("p22 wiring probe");
"""


def build_ko():
    """生成两个平凡模块并编译。缓存命中就不再编（省内存/时间）。"""
    os.makedirs(KO_DIR, exist_ok=True)
    ko_p, ko_c = "%s/%s.ko" % (KO_DIR, PROBE), "%s/%s.ko" % (KO_DIR, CTRL)
    if os.path.exists(ko_p) and os.path.exists(ko_c):
        return ko_p, ko_c
    with open("%s/Makefile" % KO_DIR, "w") as f:
        f.write("obj-m := %s.o %s.o\n" % (PROBE, CTRL))
    for n in (PROBE, CTRL):
        with open("%s/%s.c" % (KO_DIR, n), "w") as f:
            f.write(KO_TMPL % {"n": n})
    kdir = "/lib/modules/%s/build" % os.uname().release
    r = subprocess.run(["make", "-C", kdir, "M=" + KO_DIR, "modules"],
                       capture_output=True, text=True, timeout=300)
    if not (os.path.exists(ko_p) and os.path.exists(ko_c)):
        raise RuntimeError("模块编译失败：%s" % (r.stderr or r.stdout)[-400:])
    return ko_p, ko_c


def mods_text():
    return G.sh("cat /proc/modules") or ""


def mod_names(text=None):
    """当前可读的模块名集合。

    ★ 被 redact 抹平的名字整段变空格 → `split()` 后首个 token 变成 size 数字，
    真名从集合里消失。所以**集合差**能同时抓住两种失败：该藏的没藏（probe 还在）
    与不该藏的藏了（无辜名字没了）。
    """
    out = set()
    for line in (text if text is not None else mods_text()).splitlines():
        tok = line.split()
        if tok:
            out.add(tok[0])
    return out


def cleanup(pid=None, ko=()):
    """收尾必须用 rmmod 走内核接口 —— 删 .ko 文件不会卸载已加载的模块。"""
    for m in (PROBE, CTRL):
        G.sh("rmmod %s 2>/dev/null" % m)
    if pid:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
    # pin 走**精确路径**删：被 hide_names 藏住的 pin 在 shell glob 里展不开
    # （glob 过 getdents），`rm -f axp22*` 会把字面量交给 rm 而静默"成功"。
    for suf in ["_prog", "_link"] + ["_map%d" % i for i in range(8)]:
        try:
            os.unlink("/sys/fs/bpf/" + PIN + suf)
        except OSError:
            pass


def runtime_checks(listener):
    ko_p, ko_c = build_ko()
    pid = aid = None
    want = 0
    try:
        binp = e2e.gen_agent(listener, "p22")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered(listener, pid, timeout=90)
        print("[p22] agent id=%s pid=%s；模块=%s / %s" % (aid, pid, ko_p, ko_c), flush=True)

        # ★ 快照：加载 redact **之前**的全量模块名。误伤检测（T5b）靠它 ——
        #   判据要能独立报出"有无辜模块名被抹平"，而不是只靠回执文本。
        names_before = mod_names()
        if len(names_before) < 5:
            rec("T5b 前置：能读到模块名集合", "FAIL",
                "只读到 %d 个，观测手段不成立" % len(names_before))
            return

        # 载 redact（**不喂任何模块名** —— 这正是 p15 掩盖了接线缺失的地方）
        G.load(aid, "redact", 5, 1, "raw_syscalls/sys_exit", pin=PIN)
        want += 1
        ok, _ = G.wait_tasks(aid, want, 120, "load")
        G.show_bof(aid, 1)
        if not ok:
            rec("T4 前置：redact 加载", "FAIL", "观测手段没就位，后续断言无意义")
            return

        # 对照模块：**不走 ko_load**，名字理应一直可读
        G.sh("insmod %s" % ko_c)
        ctrl_loaded = os.path.isdir("/sys/module/" + CTRL)
        if not ctrl_loaded:
            rec("T4 前置：对照模块 insmod", "FAIL", "insmod 未生效，T5 的对照不成立")
            return

        # ── T4 ko_load 自己登记 ──
        G.send_bof(aid, "ko_load", struct.pack("<I", 1) + struct.pack("<I", len(ko_p))
                   + ko_p.encode())
        want += 1
        ok4, _ = G.wait_tasks(aid, want, 120, "T4")
        G.show_bof(aid, 2)
        t4 = "\n".join(G.bof_texts(aid, 2))
        rec("T4 ko_load 回执自登记", "PASS" if ("hide: %s -> ok" % PROBE) in t4 else "FAIL",
            "回执%s" % ("含 hide ok" if ("hide: %s -> ok" % PROBE) in t4 else "**未自登记**"))

        # ── T5 ★ 同一次读的带内对照 ──
        # ★ 前置不变量：probe 必须**真的加载上了**。否则"名字读不到"可以廉价地由
        #   "模块根本没加载"满足 —— 本轮首跑就是这样骗过 T5 的：T4 因 ko_load 的
        #   路径缺陷没加载成功，T5 却报 PASS。**凡"某物不可见"的断言，都必须先
        #   证明"某物存在"**，否则它测的不是隐藏，是缺失。
        probe_loaded = os.path.isdir("/sys/module/" + PROBE)
        out = mods_text()
        probe_gone = PROBE not in out
        ctrl_seen = CTRL in out
        rec("T5 同一次 cat /proc/modules：probe 不可读、对照可读",
            "PASS" if (probe_loaded and probe_gone and ctrl_seen) else "FAIL",
            "probe已加载=%s（应 True）  %s=%s（应 False）  %s=%s（应 True）" %
            (probe_loaded, PROBE, "看得见" if not probe_gone else "不可读",
             CTRL, "看得见" if ctrl_seen else "**不见了**"))

        # ── T5b ★ 误伤检测：不许抹掉任何"我们没打算抹"的名字 ──
        # 本轮真事：ko_load 读错了方向（读末行），把无辜的 cryptd 登记进了
        # hide_names，redact 转而去抹 cryptd 的名字 —— **比不抹更显眼**。
        # T4 靠回执文本抓到了它，但判据本身应当能独立报出这件事：与加载前的
        # 名字集合对差，"少了不该少的那一个"就是误伤。
        names_after = mod_names()
        collat = (names_before - names_after) - {PROBE}
        rec("T5b 无误伤：没有无辜模块名被抹平",
            "PASS" if not collat else "FAIL",
            "消失的名字=%s（允许的只有 probe）" % (sorted(collat) if collat else "无"))

        # ── T6 卸载 eBPF → 名字回来（模块仍在）──
        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN))
        want += 1
        G.wait_tasks(aid, want, 120, "T6")
        out2 = mods_text()
        still = os.path.isdir("/sys/module/" + PROBE)
        rec("T6 卸载 redact 后名字重新可读（模块仍在）",
            "PASS" if (still and PROBE in out2) else "FAIL",
            "/sys/module/%s=%s  cat 里=%s" %
            (PROBE, "在" if still else "**不在**", "可读" if PROBE in out2 else "**仍不可读**"))
    finally:
        cleanup(pid)
        left = [m for m in (PROBE, CTRL) if os.path.isdir("/sys/module/" + m)]
        rec("T7 清理：模块已卸载", "PASS" if not left else "FAIL",
            "残留=%s" % (left or "无"))


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"
    static_only = "--static-only" in argv

    static_checks()
    if not static_only:
        runtime_checks(listener)

    npass = sum(1 for _, s, _ in R if s == "PASS")
    nfail = sum(1 for _, s, _ in R if s == "FAIL")
    print("\nPASS=%d FAIL=%d" % (npass, nfail))
    print("RESULT: %s" % ("PASS" if nfail == 0 else "FAIL"))
    return 0 if nfail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
