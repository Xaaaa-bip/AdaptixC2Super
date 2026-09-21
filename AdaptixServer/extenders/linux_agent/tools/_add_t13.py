#!/usr/bin/env python3
"""给 p14 加 T13：redact 在跑时 evacuate 还能不能真清 preload（mmap 验真身）。

为什么必须有这条：p14 原本不加载 rootkit → P3-A(redact) × P3-B(evacuate) 的交互
零覆盖。而交互真实存在：redact 判定 = 「刚 access 探测过」AND「openat 带 O_CLOEXEC」，
evacuate.c:60 的 AxOpenFile(path, O_RDONLY, 0) 两个条件都不满足 → 它的读会被改写
→ 找不到要删的行。观测必须用 mmap（redact 只 hook read(2)），否则 Python 的
open().read() 同样被改写 → "读到 SO 不见了" 被误当删除成功（假绿，ISS-048 同族）。
"""
import io, sys

P = "tools/p14_systemd_evac_verify.py"
V = io.open(P, encoding="utf-8").read()

# ── 1. import mmap ────────────────────────────────────────────────────────
old_imp = "import base64, json, os, shutil, struct, subprocess, sys, time"
assert V.count(old_imp) == 1, "import 锚点 %d" % V.count(old_imp)
V = V.replace(old_imp, old_imp + "\nimport mmap")

# ── 2. T13 插在 T12 之后、kill 之前 ───────────────────────────────────────
anchor = '    rec("T12 撤离后 beacon 仍通", ok12, "回执=%r" % txt[:60])\n'
assert V.count(anchor) == 1, "T12 锚点 %d" % V.count(anchor)

T13 = '''
    # ── T13 跨特性：redact 在跑时 evacuate 还能不能**真**清 preload ─────────
    # P3-A 的 redact 判定 = 「本进程刚 access 探测过」AND「openat 带 O_CLOEXEC」。
    # evacuate.c:60 的读是 AxOpenFile(path, O_RDONLY, 0) —— 两个条件都不满足
    # → 它的读**会被改写** → 找不到要删的行。
    # ★ 观测必须用 mmap：redact 只 hook read(2)。用 open().read() 断言的话，那条读
    #   同样被改写 → "读到 SO 不见了" 会被误当成删除成功（假绿，ISS-048 同族）。
    PIN_I = "axp14i"

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

    try:
        nm = SO.encode() + b"\\x00" + b"axp14decoy"
        txt = p03.run_bof(aid, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", nm, "", b"", PIN_I), timeout=90) or ""
        ok13a = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        rec("T13a redact 已加载 (pin=%s)" % PIN_I, ok13a,
            (txt.strip().splitlines() or [""])[-1][:60])
        if ok13a:
            with open(PRELOAD, "w") as f:
                f.write(SO + "\\n/usr/lib/libnss_real.so\\n")
            p03.run_bof(aid, "evacuate", blob("preload:%s" % SO), timeout=120)
            tb = read_true(PRELOAD) if os.path.exists(PRELOAD) else b"<MISSING>"
            rec("T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（mmap 验真身）",
                SO.encode() not in tb, "真身=%r" % tb[:70])
    except Exception as ex:
        rec("T13 跨特性：redact x evacuate", False, "exception: %s" % ex)
    finally:
        try:
            p03.run_bof(aid, "ebpf_unload", g10.pack_unload(PIN_I), timeout=60)
        except Exception:
            pass
        cleanup()
        time.sleep(1)
'''
V = V.replace(anchor, anchor + T13)
io.open(P, "w", encoding="utf-8").write(V)

# ── 3. 自校验：语法必须能过 ───────────────────────────────────────────────
import py_compile
py_compile.compile(P, doraise=True)
print("OK T13 已插入，语法校验通过；文件 %d 行" % (V.count("\n") + 1))
