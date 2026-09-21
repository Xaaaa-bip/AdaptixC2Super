#!/usr/bin/env python3
"""p14 三处修补：

① T7 假绿：`open(PRELOAD).read()` 在 redact 在跑时是**循环论证**（那条读同样被
   改写）→ 改用 mmap 真身（read_true）。
② 加 T14：把 p16 的别名面（//  /./  相对  open(2)）搬进**跨特性窗口**再跑一遍 ——
   撤离/清理常常在 rootkit 已加载时下发。
③ T13 注释已过时：它描述的是修 ISS-054 **之前**的失效理由。现在 T13 考的是
   **豁免通道**（AxAccess 探测 + O_CLOEXEC），绿= Fix C 生效。
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P = "tools/p14_systemd_evac_verify.py"


def rd(p):
    with io.open(os.path.join(ROOT, p), encoding="utf-8") as f:
        return f.read()


def wr(p, s):
    with io.open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="") as f:
        f.write(s)
    print("  [ok] %s -> %d 行" % (p, s.count("\n") + 1))


def sub(V, old, new, tag):
    c = V.count(old)
    assert c == 1, "[%s] 锚点命中 %d 次（要求 1）" % (tag, c)
    return V.replace(old, new)


V = rd(P)

# ── ① ctypes 进 import 行
V = sub(V, "import base64, json, os, shutil, struct, subprocess, sys, time",
        "import base64, ctypes, json, os, shutil, struct, subprocess, sys, time",
        "import")

# ── ② 读法三件套提到模块级（T7 与 T13/T14 共用）
V = sub(V, "PRELOAD_BAK = None", r'''# ── 读 preload 的三种读法 ────────────────────────────────────────────────────
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


PRELOAD_BAK = None''', "helpers")

# ── ③ docstring 补 T13/T14
V = sub(V, "  T12 回归：agent beacon 仍通",
        "  T12 回归：agent beacon 仍通\n"
        "  T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（考**豁免通道**）\n"
        "  T14 跨特性别名面：//  /./  相对  open(2) 四条读法全被抹平，mmap 真身未变",
        "docstring")

# ── ④ T7 改 mmap 真身
V = sub(V, '''    pre = open(PRELOAD).read() if os.path.exists(PRELOAD) else "<MISSING>"
    rec("T7 evacuate preload: 目标行删、保留行留、文件仍在",
        os.path.exists(PRELOAD) and SO not in pre and "libnss_real.so" in pre,
        "内容=%r" % pre)''',
        '''    pre = read_true(PRELOAD) if os.path.exists(PRELOAD) else b"<MISSING>"
    rec("T7 evacuate preload: 目标行删、保留行留、文件仍在",
        os.path.exists(PRELOAD) and SO.encode() not in pre and b"libnss_real.so" in pre,
        "真身=%r" % pre)''', "T7")

# ── ⑤ T13 注释纠正 + read_true 提到模块级（删内嵌定义）
V = sub(V, '''    # ── T13 跨特性：redact 在跑时 evacuate 还能不能**真**清 preload ─────────
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

    try:''',
        '''    # ── T13/T14 跨特性：redact 在跑时 evacuate 还能不能**真**清 preload ─────
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

    try:''', "T13-comment")

# ── ⑥ 加 T14（紧跟在 T13 断言之后）
V = sub(V, '''            rec("T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（mmap 验真身）",
                SO.encode() not in tb, "真身=%r" % tb[:70])''',
        r'''            rec("T13 跨特性：redact 在跑时 evacuate 仍能真删 preload 行（mmap 验真身）",
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
                "漏=%s 真身仍有SO=%s" % (leaks or "无", SO.encode() in a5))''',
        "T14")

wr(P, V)
import py_compile
py_compile.compile(os.path.join(ROOT, P), doraise=True)
print("\n[OK] 语法检查通过")
