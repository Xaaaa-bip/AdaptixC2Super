#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p21_pin_autohide_verify.py — P8 验收：pin 文件自动隐藏 + 退役「扫 /sys/fs/bpf」

被测改动（P8）：
  ebpf_load §6   登记内核对象 id 改用**手上的 fd**（不再扫 /sys/fs/bpf）
  ebpf_load §6c  加载后自动把 <pin>_prog / _map0..7 (+_link) 写进 hide_names
  core           AxHidePinOpen / AxHideFdAdd / AxHideFdDel / AxHideFdClose / AxHideNameDel
  hide_del BOF   从 hide_names 删回名字（长生命周期那张表的槽位回收）

★ 为什么必须有阳性对照（P4/P5/P7 同一条原则的第四次应用）：
  `ls /sys/fs/bpf` 在「目录本来就是空的」与「东西全被藏起来了」两种情况下**输出
  完全一样** —— 而本测试环境的基线恰好就是空目录。所以：
    T1 先往目录里放一个**不受 hide_names 影响**的对照项，证明"这目录真列得出东西"；
    T4 再把对照项藏起来（消失）、T6 再删回来（复现）—— 证明过滤器确实作用在
    这个目录上、且删键能撤销。没有这三步，T2 的"看不见"可能只是"什么都没加载"。

★ 量化判据：`bpftool map dump pinned /sys/fs/bpf/<pin>_map0` 走的是**按路径
  BPF_OBJ_GET**，不是 GET_FD_BY_ID，所以 bpf_filter **不过滤**它 —— pin 文件已经
  被隐藏了，照样读得到 hide_names 的**元素个数**。这比数回执行可靠得多：
  回执是"我以为写了几个"，map dump 是"表里真有几条"。

  T1  ★ 阳性对照：/sys/fs/bpf 里放一个对照 map -> ls 看得见
  T2a ★ 半边一：**先**加载 bpf_filter（非 dirent）-> 它自己**藏不住**（无人可写）
  T2  ★ 主题：再加载 hide（dirent，重放全部已登记 pin）-> 两个前缀在 ls 里都没了，
          但按路径 9+9 个后缀全在（"藏起来"与"对象还在"必须分开观测）
  T3  ★ D1 未回归：bpftool prog list 的 id 集合与基线一致（§6e 跨实例登记）
  T3b   同上 · map list 行数也回落基线（map id 同样进了 sink）
  T4  ★ 过滤器生效对照：hide_add <pin> <CTL> -> <CTL> 从 ls 消失
  T5  map 计数：加载后 hide_names 恰好 18 条（9×2，回执 18/18）
  T6  ★ 新 API：hide_add 使计数 +1、hide_del 使计数回到原值，且 <CTL> 回到 ls
  T7  卸载两个 pin 后：9+9 个 <pin>_* 按路径全部不存在，<CTL> 仍在（不误伤）
  T8  卸载后：bpftool prog list 回落基线

★ 加载顺序为什么必须是「bpf_filter 先、hide 后」：
  §6c 的两半是「我是 dirent → 认领并重放」与「dirent 已加载 → 把自己的名字写进去」。
  只加载一个模块只能碰到其中一半，而且**恰好碰不到难的那一半**。反序（hide 先）
  走的是简单半边（hide 当场认领、只写自己）；正序（bpf_filter 先）走的是重放半边
  —— 先加载的那个模块在当时**无表可写**，它的名字必须靠后加载者补上。
  T2a 就是这一步的阳性对照：先加载后它**必须仍然可见**，否则"T2 全消失"可能
  只是加载顺序碰巧、而不是重放真的发生了。

用法: p21_pin_autohide_verify.py [--listener TestHTTP] [--keep]
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                       # noqa: E402
import gap10_rootkit_verify as G                 # noqa: E402  复用观测原语与 BOF 打包

# ★ P8 起要**同时**加载两个模块，顺序是刻意挑的（见文件头「加载顺序」）：
#   PIN2/bpf_filter **先** → 它不是 getdents 过滤器，加载时"没有 dirent 可写"，
#                            自己一条名字都写不进去（回执 0/0）—— 最容易漏测的半边；
#   PIN/hide        **后** → 它认领 dirent，并把**已登记的全部 pin**重放一遍 → 9×2。
# 两个 pin 名互不为前缀，但都挂在 axg21 下 —— 泄漏检查按 PREFIX 数才不漏项。
PIN  = "axg21h"           # hide 实例 == getdents 过滤器 == hide_names 的**读方**
PIN2 = "axg21f"           # bpf_filter 实例 == 只写不读（它读的是 hidden_bpf_ids）
PREFIX = "axg21"          # 泄漏检查的公共前缀（两个 pin 名都挂在它下面）
# ⚠ 对照项**绝不能**用这个前缀：第一版叫 axg21ctl，于是 T2 的"泄漏"里永远躺着它
#   —— 对照项本来就该看得见，却被前缀匹配抓成泄漏，T2 无条件假红。改叫 ctrlg21
#   （与 gap10/g18 的 ctrlg10/ctrlg18 同款命名）。**判据的"应当可见"与"应当隐藏"
#   两个集合必须不相交**，否则分辨力直接归零。
CTL = "ctrlg21"           # 阳性对照：bpftool map create 出来的一个无关 map
MOD,  TARGET  = "hide",       "syscalls/sys_exit_getdents64"
MOD2, TARGET2 = "bpf_filter", "syscalls/sys_enter_bpf"

# ebpf_load §6c 登记的后缀（tracepoint 加载没有 _link）—— 顺序即回执顺序。
SUFFIXES = ["_prog", "_map0", "_map1", "_map2", "_map3",
            "_map4", "_map5", "_map6", "_map7"]
BY_LINK = "_link"

R = []


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-46s %s" % (status, item, note), flush=True)


# ─────────────────────────── 观测原语 ───────────────────────────
def exists(name):
    """按**路径**判存在。路径 stat 走的是 sys_newlstat，不是 getdents64 ——
    hide_names 过滤不到它。这正是「藏起来」与「还能用」能共存的原因（ISS-061）。"""
    return G.sh("test -e /sys/fs/bpf/%s && echo Y || echo N" % name) == "Y"


def hide_keys(pin):
    """hide_names 的元素个数。读不到返回 -1（不是 0 —— 0 是合法的空表读数，
    把"读不到"和"空表"混成一个值，正是这套判据最容易犯的错）。"""
    out = G.sh("bpftool map dump pinned /sys/fs/bpf/%s_map0 2>/dev/null" % pin)
    m = re.search(r"Found (\d+) elements", out)
    return int(m.group(1)) if m else -1


def hidden_names(names, ctl="ctlvis21"):
    """**行为判据**：把 names 做成同名空文件，看哪些从目录枚举里消失。
    消失 = 该名字在 hide_names 里（getdents64 过滤按 dirent 名**全局**生效、无目录
    限定 —— p13 T4 已经把这个"误伤面"断言成已知代价）。返回 (命中的集合, 对照是否可见)。

    ★ 为什么不数 `bpftool map dump` 的元素个数：那张表**不止装 pin 名** —— agent
    自己的 pid 也在里面（B10 自隐藏）。绝对个数断言会把"另一个特性正常工作"判成
    本测试失败，而且**成不成功取决于噪声节律有没有落进两次读数之间** → 假红还是
    **随机的**。改成点名 + 看实际效果之后，表里多谁都不影响本判据，顺带白拿一个
    必然成立的阳性对照（ctl 必须看得见）。
    """
    d = G.sh("mktemp -d /tmp/axn21_XXXXXX").strip()
    if not d.startswith("/tmp/axn21_"):
        return set(), False
    G.sh("d=%s; for n in %s %s; do : > \"$d/$n\"; done" % (d, " ".join(names), ctl))
    got = set(G.sh("ls -A %s" % d).split())      # ls 走 getdents64 —— 与对手同一条路
    G.sh("rm -rf %s" % d)
    return set(names) - got, ctl in got


def visible():
    return G.pins()


def map_count():
    """`bpftool map list` 的行数。bpf_filter 的钩子拦 MAP_GET_FD_BY_ID，所以
    登记进 sink 的 map id 会整条从枚举里消失 —— 这是 prog 之外**第二个**可量化的
    D1 观测面（prog 数只覆盖"程序藏了没"，map 数才覆盖"表藏了没"）。"""
    out = G.sh("bpftool map list 2>/dev/null | grep -c '^[0-9]'")
    return int(out.strip() or -1) if out.strip().lstrip("-").isdigit() else -1


def make_ctl(name=CTL):
    """在 /sys/fs/bpf 里放一个无关 map 当**阳性对照**。p19 也借它用（参数化名字）。"""
    G.sh("rm -f /sys/fs/bpf/%s" % name)
    G.sh("bpftool map create /sys/fs/bpf/%s type hash key 4 value 1 entries 1 "
         "name axg21c 2>/dev/null" % name)


def clean_ctl():
    G.sh("rm -f /sys/fs/bpf/%s" % CTL)


def clean_pins():
    """**按显式路径**清 pin：ls 已经列不出被隐藏的 pin，扫目录式的清理会静默漏掉
    它们（ISS-062）。两个 pin 都要清 —— 漏清一个，下一轮 T2a 的阳性对照就假红。"""
    for p in (PIN, PIN2):
        for s in SUFFIXES + [BY_LINK]:
            G.sh("bpftool link detach pinned /sys/fs/bpf/%s%s 2>/dev/null" % (p, s))
            G.sh("rm -f /sys/fs/bpf/%s%s" % (p, s))


def hid_line(t):
    """从回执里抠出 §6c 的「pin names hidden X/Y -> dst=...」那一行。
    X/Y 是**加载器以为**写了几个；真读数一律以 hide_keys() 的 map dump 为准。"""
    for line in t.splitlines():
        if "pin names hidden" in line:
            return line.strip()
    return ""


def pack_names(*fields):
    """hide_add / hide_del 的参数 blob：`[4B count][ (4B len + bytes)* ]`。
    少写 count 头就会整体**错位 4 字节**且**不报错**（ISS-005），所以这里不手搓。"""
    out = struct.pack("<I", len(fields))
    for f in fields:
        out += G._sf(f)
    return out


def run_bof(aid, name, blob, want=1, timeout=120):
    """发一个 BOF 并等它完成，返回全部回执文本。"""
    G.send_bof(aid, name, blob)
    G.wait_tasks(aid, want, timeout, name)
    time.sleep(1)
    return "\n".join(G.bof_texts(aid, 2))


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"

    if not os.path.exists("%s/hide_del.o" % G.BOF_DIR):
        print("FATAL: hide_del.o 未部署（先 bash tools/deploy.sh）")
        return 2

    pid = aid = None
    try:
        # 清场 + 基线
        clean_pins()
        make_ctl()
        base_prog = G.prog_ids()
        base_map = map_count()

        binp = e2e.gen_agent(listener, "p21")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered(listener, pid, timeout=90)
        print("[p21] agent id=%s pid=%s 基线 prog=%d" % (aid, pid, len(base_prog)), flush=True)

        # ── T1 ★ 阳性对照：对照项必须看得见 ──
        v1 = visible()
        rec("T1 ★ 阳性对照 · /sys/fs/bpf 里的对照 map 看得见",
            "PASS" if CTL in v1 else "FAIL",
            "ls=%s" % (sorted(v1) or "（空）"))

        # ── T2a ★ 半边一（阳性对照）：先加载的 bpf_filter 藏不住自己 ──
        # 它不是 getdents 过滤器 → §6c 里 dst=NULL、want=0，"写了 0 条"是**正确行为**。
        # 没有这一步，T2 的"全消失"就分不清是重放生效还是碰巧。
        t2a = run_bof(aid, "ebpf_load",
                      G.pack_load("%s/%s.insns.bin" % (G.BPF_OUT, MOD2),
                                  "%s/%s.mapreloc.txt" % (G.BPF_OUT, MOD2),
                                  5, 1, TARGET2, "", "", b"", PIN2), timeout=150)
        time.sleep(1)
        v2a = visible()
        seen2 = [x for x in v2a if x.startswith(PIN2)]
        rec("T2a ★ 先加载 bpf_filter（非 dirent）· 自己藏不住（无人可写）",
            "PASS" if seen2 else "FAIL",
            "ls 里可见 %d 条 | 回执=%s" % (len(seen2), hid_line(t2a) or "（无隐藏行）"))

        # ── T2 ★ 主题：后加载的 hide 认领 dirent 并**重放已登记 pin** ──
        t2 = run_bof(aid, "ebpf_load",
                     G.pack_load("%s/%s.insns.bin" % (G.BPF_OUT, MOD),
                                 "%s/%s.mapreloc.txt" % (G.BPF_OUT, MOD),
                                 5, 1, TARGET, "", "", b"", PIN), timeout=150)
        time.sleep(1)
        v2 = visible()
        leaked = [x for x in v2 if x.startswith(PREFIX)]
        # 按路径把九个后缀逐个点一遍 —— "藏起来"和"对象没了"必须分开观测
        alive = [s for s in SUFFIXES if exists(PIN + s)]
        alive2 = [s for s in SUFFIXES if exists(PIN2 + s)]
        ok2 = (not leaked) and len(alive) == len(SUFFIXES) \
            and len(alive2) == len(SUFFIXES)
        rec("T2 ★ 两个模块加载后 ls 无 <pin>*，按路径 9+9 个后缀全在",
            "PASS" if ok2 else "FAIL",
            "泄漏=%s | 按路径 %d+%d/%d | 回执=%s"
            % (leaked or "无", len(alive), len(alive2), len(SUFFIXES),
               hid_line(t2) or "（无隐藏行）"))

        # ── T3 ★ D1 未回归：prog 仍从 bpftool 枚举里消失 ──
        # 两个模块同载是本阶段新加的强度：只加载 bpf_filter 时，它藏住的是**自己**
        # 的 prog（fd 直登记就够），跨实例那一半根本没被考到。§6e 的 id 注册表就是
        # 被这条断言逼出来的。
        p3 = G.prog_ids()
        rec("T3 ★ D1 未回归 · bpftool prog list 与基线一致（§6e 跨实例登记生效）",
            "PASS" if p3 == base_prog else "FAIL",
            "基线=%d 现在=%d 多出=%s" % (len(base_prog), len(p3), sorted(p3 - base_prog)))

        # ── T3b map 侧同理：两个模块的 8 张表都该从 bpftool map list 消失 ──
        m3 = map_count()
        rec("T3b map list 与基线一致（两个模块的 map id 都进了 sink）",
            "PASS" if m3 == base_map else "FAIL",
            "基线=%s 现在=%s" % (base_map, m3))

        # ── T5 点名断言（放在 T4 之前取基准）──
        # 两个 pin × 9 个后缀 = 18，**逐个点名**：判据是"这个名字真的被过滤掉了"，
        # 不是"表里恰好有 18 条"。理由见 hidden_names()：表里还有 B10 写的 agent
        # 自身 pid，数总数会把别人的正常行为判成本测试失败（且随机假红）。
        k5 = hide_keys(PIN)
        want5 = [p + s for p in (PIN, PIN2) for s in SUFFIXES]
        hid5, ctl5 = hidden_names(want5)
        miss5 = [n for n in want5 if n not in hid5]
        rec("T5 加载后 18 个 pin 名**逐个**生效（9×2，同名文件枚举被过滤）",
            "PASS" if (not miss5 and ctl5) else "FAIL",
            "命中 %d/18 | 漏=%s | 对照文件可见=%s | 表内元素=%d（共享表：B10 的 tick 会让它在 18/19 间跳，**只作参考不作判据**）"
            % (len(hid5), miss5 or "无", ctl5, k5))

        # ── T4 ★ 过滤器生效对照：把对照项藏起来，它必须消失 ──
        t4 = run_bof(aid, "hide_add", pack_names(PIN, CTL))
        v4 = visible()
        rec("T4 ★ 过滤器生效对照 · hide_add 后对照 map 从 ls 消失",
            "PASS" if (CTL not in v4 and "hidden '%s'" % CTL in t4) else "FAIL",
            "ls=%s" % (sorted(v4) or "（空）"))

        # ── T6 ★ 新 API：加入即生效、删掉即复现（点名判据，不依赖表的总条数）──
        k6 = hide_keys(PIN)
        t6 = run_bof(aid, "hide_del", pack_names(PIN, CTL))
        time.sleep(1)
        k7 = hide_keys(PIN)
        v6 = visible()
        # CTL 这个名字此刻已被 T4 的 hide_add 写进表（同名文件应被过滤）；del 之后
        # 必须恢复可见。两侧都**点名**，B10 什么时候往表里写 pid 都不影响本条。
        hid6, ctl6 = hidden_names([CTL])
        ok6 = (CTL not in hid6) and ctl6 and (CTL in v6) and ("unhidden" in t6)
        rec("T6 ★ hide_del 后名字复现（点名）且对照项回到 ls",
            "PASS" if ok6 else "FAIL",
            "k5=%d k6=%d k7=%d | CTL 仍被过滤=%s | 对照项回到 ls=%s"
            % (k5, k6, k7, CTL in hid6, CTL in v6))

        # ── T7 卸载：两个 pin 的文件按路径消失，对照项不误伤 ──
        t7 = run_bof(aid, "ebpf_unload", G.pack_unload(PIN, ""), timeout=150)
        t7b = run_bof(aid, "ebpf_unload", G.pack_unload(PIN2, ""), timeout=150)
        time.sleep(2)
        left = [s for s in SUFFIXES if exists(PIN + s)]
        left2 = [s for s in SUFFIXES if exists(PIN2 + s)]
        v7 = visible()
        rec("T7 卸载后 9+9 个 pin 文件按路径不存在，对照项未误伤",
            "PASS" if (not left and not left2 and CTL in v7
                       and "unpinned" in t7 and "unpinned" in t7b) else "FAIL",
            "残留=%s/%s | ls=%s" % (left or "无", left2 or "无", sorted(v7) or "（空）"))

        # ── T8 prog 回落基线 ──
        p8 = G.prog_ids()
        rec("T8 卸载后 bpftool prog list 回落基线",
            "PASS" if p8 == base_prog else "FAIL",
            "基线=%d 现在=%d 残留=%s" % (len(base_prog), len(p8), sorted(p8 - base_prog)))

        print("", flush=True)
        npass = sum(1 for _, s, _ in R if s == "PASS")
        nfail = sum(1 for _, s, _ in R if s == "FAIL")
        print("PASS=%d FAIL=%d" % (npass, nfail))
        print("RESULT: %s" % ("PASS" if nfail == 0 else "FAIL"))
        return 0 if nfail == 0 else 1
    finally:
        if pid:
            try:
                e2e.kill(pid)
            except Exception:
                pass
        if "--keep" not in argv:
            clean_pins()
            clean_ctl()


if __name__ == "__main__":
    sys.exit(main())
