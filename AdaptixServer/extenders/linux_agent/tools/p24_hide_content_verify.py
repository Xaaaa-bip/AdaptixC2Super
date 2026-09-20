#!/usr/bin/env python3
"""P9-9.1 类A 内容层隐藏验收：登记在 hide_names 里的产物，**读不到**了。

被测机制：`redact.c` 的闸门从"两个写死的文件名"（`ld.so.preload` / `/proc/modules`）
换成"整张 `hide_names`"—— open 时按**路径最后一个分量**查表，命中记 `AX_KIND_HIDE`，
read 时**整块抹平**（不查表、不读内容：整个文件都是我们造的产物）。

为什么单列脚本而不是并进 p15/p16：那两个判的是 PRELOAD 那条**选择性**抹平
（只换词、保留邻行），本脚本判的是类A 的**整块**抹平。语义不同的两条路径混在
一个台面里，"哪条红了"就不可读了。

**枚举层（`ls` 列不出）不在本脚本判据里**：那由 `hide.insns`（getdents64 过滤）负责，
已被 gap10 / p19 / p21 覆盖；本脚本只加载 redact、只判**内容层**。
两者合起来才是 §2b 那句话：「列不出来 **且** 读不到」。

  T0   基线（加载前）：豁免通道读 JOB 能看见 MARK —— 先证明观测手段有效
  T-load  redact 已加载（pin=axp24；names 只播 axp24job，**不播** axp24ctl）
  T1   ★核心：工具读 JOB 读不到 MARK（整块抹平）
  T2   等长 + 文件本体未变（mmap 读的 md5 与加载前一致 ⇒ 是读时改写，不落盘）
  T3   ★★阳性对照：**未登记**的同目录邻居 CTL 读到**真内容**
       （没有这条，"读不到"与"整个 /etc/cron.d 被抹平"就分不开）
  T4   ★豁免通道：`access(2)` + `O_CLOEXEC` 读到**真值**（我们自己的工具不能瞎）
  T4b  一次性：旗标被消费后，紧接的第二次 open 又回到被抹状态
  T5   已知代价（据实记录，不是"通过"）：同**基名**异目录的文件也被抹（闸门无目录限定）
  T6   非目标不误伤：cat /etc/hostname 原样
  T7   unload 后复原（证明是 BPF 在读时改写）
  T8   回归：beacon 仍通

  T9   ★★ 长路径组：`/etc/systemd/system/<unit>.service`（36 字符 > 31）
  T9b  同目录同长度的**未登记**邻居仍读到真值
       ★ 为什么单列这一组（本脚本第一版没有它，是缺陷）：闸门里那条
       `if (pn > 31u) return 0;` 原本在类A 分支**之前** —— 19 字符的 cron 路径
       过得去、33 字符的 systemd unit **整个闸门都进不去**。只用短路径的台面
       会**全绿**，而设计稿点名的主力落点静默失效。判据的覆盖面必须覆盖
       被测对象的**定义域**，不能只覆盖"我随手写的那个例子"。
"""
import ctypes
import hashlib
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10            # pack_load / pack_unload / BPF_OUT
import p03_hide_link_verify as p03            # run_bof：精确参数字节 + 按 a_task_id 等 107
import p15_redact_verify as p15               # read_exempt / read_true_mmap / getuid_ok / kill

PIN = "axp24"
LISTENER = "TestHTTP"
JOB = "/etc/cron.d/axp24job"                  # 已登记：走类A 整块抹平（短路径 19 字符）
CTL = "/etc/cron.d/axp24ctl"                  # 未登记：同目录阳性对照
DECOY = "/tmp/axp24job"                       # 同基名异目录 → 预期也被抹（T5 已知代价）
# ★ T9 长路径组：/etc/systemd/system/ = 20 字符，+16 = 36 > 31（长度闸门的边界外）
UNIT = "/etc/systemd/system/axp24p24.service"
UNITCTL = "/etc/systemd/system/axp24ctl.service"
JOBNAME = "axp24job"
UNITNAME = "axp24p24.service"
UNITCTLNAME = "axp24ctl.service"
MARK = "AXP24MARKER"
LIBC = ctypes.CDLL("libc.so.6", use_errno=True)
R_OK = 4
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def read_tool(path, n=4096):
    """工具路径：`os.open` **不带** `O_CLOEXEC`（见 p15.read_tracked 头注）→ 记表 → 被抹。

    不能用内建 `open()`：Python 3 的文件对象默认带 `O_CLOEXEC`（PEP 446）。不过
    在**新判据**下两者结局一样 —— 豁免还要"本进程刚探测过"，工具没有那次探测。
    """
    fd = os.open(path, os.O_RDONLY)
    try:
        return os.read(fd, n)
    finally:
        os.close(fd)


def access_arm(path):
    """`access(2)` 打旗标。**必须走 libc**：redact 只认 nr 21，而 CPython 的
    `os.access` 在部分版本走 faccessat —— 那样旗标永远打不上，T4 会假红。"""
    LIBC.access(path.encode(), R_OK)


def read_real(path, n=4096):
    """★ 豁免通道 —— 逐字节复刻 `AxReadFileReal`（bof_api.h）：
    先 `access(2)` 打旗标，**紧接**一条带 `O_CLOEXEC` 的 open 把旗标消费掉。

    顺序不能反，中间也不能插别的 open：旗标在 open 返回时被**一次性**消费。
    这台通道的用途是"我们自己的工具要读真值"（撤离 / 幂等检查），
    对手要伪造它就得先 `access(R_OK)` 再 `open(O_CLOEXEC)` —— 那正是 ld.so 的形态。
    """
    access_arm(path)
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    try:
        return os.read(fd, n)
    finally:
        os.close(fd)


def main():
    pid0 = 0
    loaded = False
    a0 = None
    try:
        # ── 造现场：一个**已登记**的 job、一个**未登记**的同目录邻居、一个同基名异目录诱饵 ──
        with open(JOB, "w") as f:
            f.write("*/3 * * * * root /tmp/axp24-payload # %s\n" % MARK)
        with open(CTL, "w") as f:
            f.write("*/7 * * * * root /tmp/axp24-ctl # %s\n" % MARK)
        with open(DECOY, "w") as f:
            f.write("harmless plain file %s\n" % MARK)
        # T9 长路径组（systemd unit 的形状：ExecStart + 注释）
        unit_body = ("[Service]\nExecStart=/tmp/axp24-payload # %s\n" % MARK)
        with open(UNIT, "w") as f:
            f.write(unit_body)
        with open(UNITCTL, "w") as f:
            f.write(unit_body)

        job_sz = os.stat(JOB).st_size
        unit_sz = os.stat(UNIT).st_size
        job_md5 = hashlib.md5(p15.read_true_mmap(JOB)).hexdigest()
        print("[env] JOB=%s (%d 字节) / CTL=%s / DECOY=%s"
              % (JOB, job_sz, CTL, DECOY), flush=True)
        # 前置断言：长度假设本身要被检查 —— 名字改短了，这组就**不再压到长度闸门**，
        # 必须当场说出来，而不是继续绿着（判据失效比判据变红更危险）。
        rec("T9-prep 长路径组确实 > 31 字符（压到长度闸门）",
            len(UNIT) > 31 and len(UNITCTL) > 31,
            "UNIT=%d / UNITCTL=%d" % (len(UNIT), len(UNITCTL)))

        bin0 = e2e.gen_agent(LISTENER, "p24a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] A0 aid=%s pid=%d" % (a0, pid0), flush=True)

        # ── T0：先证明观测手段有效（加载前读得到）────────────────────────
        rec("T0 基线：加载前豁免通道读得到 MARK", MARK.encode() in read_real(JOB),
            "%d 字节" % job_sz)

        # ── 加载 redact：names 播已登记的 JOBNAME 与长路径组 UNITNAME ────
        names = (JOBNAME + "\x00" + UNITNAME + "\x00").encode()
        txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", names, "", b"", PIN),
            timeout=90) or ""
        loaded = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        rec("T-load redact 已加载 (pin=%s)" % PIN, loaded,
            (txt.strip().splitlines() or [""])[-1][:64])
        if not loaded:
            print("[abort] redact 未加载（verifier 拒绝？见上一行回执）", flush=True)
            return 1
        time.sleep(1)

        # ── T1：核心 —— 整块抹平 ────────────────────────────────────────
        tr = read_tool(JOB)
        rec("T1 ★核心：工具读 JOB 读不到 MARK（整块抹平）", MARK.encode() not in tr,
            "读回 %d 字节，前 16 = %r" % (len(tr), tr[:16]))

        # ── T2：等长 + 文件本体未变 ─────────────────────────────────────
        rec("T2 等长：抹平后字节数与真身一致", len(tr) == job_sz, "%d/%d" % (len(tr), job_sz))
        rec("T2b 文件本体未变：mmap 读的 md5 与加载前一致",
            hashlib.md5(p15.read_true_mmap(JOB)).hexdigest() == job_md5, job_md5[:8])

        # ── T3：★★阳性对照 —— 未登记的同目录邻居必须读到真内容 ──────────
        ct = read_tool(CTL)
        rec("T3 ★★阳性对照：未登记的邻居 CTL 读到真内容", MARK.encode() in ct,
            "读回 %d 字节" % len(ct))
        rec("T3b 邻居真值仍在本体里：mmap 读 CTL 也看得见 MARK",
            MARK.encode() in p15.read_true_mmap(CTL))

        # ── T4：★豁免通道 —— 我们自己的工具必须拿到真值（ISS-076 的修法靠它）──
        rr = read_real(JOB)
        rec("T4 ★豁免通道：access(2)+O_CLOEXEC 读到真值", MARK.encode() in rr,
            "读回 %d 字节" % len(rr))

        # 一次性消费：一次探测只放行**一条** open
        access_arm(JOB)
        fdA = os.open(JOB, os.O_RDONLY | os.O_CLOEXEC)
        a_ok = MARK.encode() in os.read(fdA, 4096)
        os.close(fdA)
        fdB = os.open(JOB, os.O_RDONLY | os.O_CLOEXEC)
        b_txt = os.read(fdB, 4096)
        os.close(fdB)
        rec("T4b 一次性：旗标消费后第二条 open 又回到被抹状态",
            a_ok and MARK.encode() not in b_txt,
            "第1条=%s 第2条=%s" % (a_ok, MARK.encode() in b_txt))

        # ── T5：已知代价（据实记录）─────────────────────────────────────
        dc = read_tool(DECOY)
        rec("T5 已知代价：同基名异目录的文件也被抹（闸门无目录限定）",
            MARK.encode() not in dc, "对照 %s" % DECOY)

        # ── T6：非目标不误伤 ────────────────────────────────────────────
        try:
            host = subprocess.run(["/usr/bin/cat", "/etc/hostname"], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, timeout=20).stdout
        except Exception:
            host = b""
        rec("T6 非目标不误伤：cat /etc/hostname 原样",
            host == p15.read_exempt("/etc/hostname"), host.strip()[:32])

        # ── T9：★★ 长路径组（>31 字符）—— 本脚本第一版缺的就是这一组 ─────────
        ut = read_tool(UNIT)
        rec("T9 ★★长路径组：工具读 systemd unit 读不到 MARK", MARK.encode() not in ut,
            "%s (%d 字节) 读回 %d" % (UNIT, unit_sz, len(ut)))
        rec("T9b 长路径组的等长：字节数与真身一致", len(ut) == unit_sz,
            "%d/%d" % (len(ut), unit_sz))
        uct = read_tool(UNITCTL)
        rec("T9c ★阳性对照：同目录同长度的未登记邻居读到真内容",
            MARK.encode() in uct, "%s 读回 %d" % (UNITCTL, len(uct)))

        # ── T7：unload 后复原 ──────────────────────────────────────────
        try:
            p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
            loaded = False
        except Exception as e:
            print("[warn] unload: %s" % e, flush=True)
        time.sleep(1)
        after = read_tool(JOB)
        rec("T7 unload 后复原：又能读到 MARK", MARK.encode() in after,
            "%d 字节" % len(after))

        # ── T8：回归 ───────────────────────────────────────────────────
        rec("T8 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))

        return 0 if all(R) else 1
    finally:
        # 清场顺序：**先卸模块**（它会影响全机的 read），再删文件、杀进程。
        try:
            if loaded and a0:
                p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
        except Exception as e:
            print("[warn] unload(final): %s" % e, flush=True)
        for p in (JOB, CTL, DECOY, UNIT, UNITCTL):
            try:
                os.remove(p)
            except OSError:
                pass
        print("[env] 清场（JOB/CTL/DECOY/UNIT/UNITCTL 均已删除）= %s"
              % all(not os.path.exists(p) for p in (JOB, CTL, DECOY, UNIT, UNITCTL)),
              flush=True)
        p15.kill(pid0)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)


if __name__ == "__main__":
    sys.exit(main())
