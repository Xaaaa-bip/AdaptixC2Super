#!/usr/bin/env python3
"""P9 / MODULES 内容层探针（一次性诊断，不进回归批次）。

**背景**：p15 的两个新判据（T3 / T8 D3）报红 —— `/proc/modules` 首行的模块名
（`mods[0]`，字节偏移 0）没有被抹掉。而静态推演的结论是**应该命中**：
  · open 分支：`ax_tail_modules("/proc/modules", 13)` ⇒ `s[5]='/'` ⇒ 记录 MODULES ✓
  · read 分支：`n = min(2741, 128) = 128` ⇒ `lim = 96` ⇒ p=0 的 `run` 保持 1
  · `ax_scan` p=0：字节 0..7 = "tcp_diag"，第 8 字节是空格（32 < 33）⇒ run 归零
    ⇒ `km` = "tcp_diag" + 56 个 0，与 `<names>` 登记键逐字节一致
推演与实测矛盾 ⇒ 不再猜，**把两张表 dump 出来当物证**：

  map0 = `hide_names`（键 64 字节，值 1 字节）—— 词到底登记进去没有
  map7 = `redact_fds`（键 = `(tgid<<32)|fd`，值 = kind）—— fd 到底被记成什么

★ 判别力设计：**map7 必须在 fd 还开着的时候 dump**（close 分支会删表）。
  于是 S4 用 `os.open` 拿住 fd、`os.read` 之后**不关**，先 dump 再关。
  两张表的取值把三种候选成因一刀切开：
    map0 无键                 ⇒ 成因 = 登记
    map7 无键 / kind≠1(MODULES) ⇒ 成因 = open 分支判据
    两表都对但内容没被抹       ⇒ 成因 = read 分支/写回（再看 bpftrace）

★ 阴性/阳性对照（ISS-083 的教训：断言必须建立在"该发生的事确实会发生"之上）：
    S1 加载前读得到目标词      —— 基线
    S5 `cat` 通道             —— 对手最常用的那条；与 S4 结论不同即说明
                                两条通道走的根本不是同一条 syscall

★ 所有 `str` → 字节一律用 `bpftool -j`（JSON），拿到的是**精确字节**，
  不是给人看的十六进制文本 —— 这样才敢做"逐字节一致"的断言。
"""
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e
import gap10_rootkit_verify as g10
import p03_hide_link_verify as p03
import p15_redact_verify as p15

PIN = "axp26"
LISTENER = "TestHTTP"
MODULES = "/proc/modules"
MARKER = "AXP26MARKER"
PINROOT = "/sys/fs/bpf/%s_map%%d" % PIN
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def pyread(n=4096):
    fd = os.open(MODULES, os.O_RDONLY)
    try:
        return os.read(fd, n)
    finally:
        os.close(fd)


def catread():
    return subprocess.run(["/usr/bin/cat", MODULES], stdout=subprocess.PIPE).stdout


def brief(b):
    return "len=%d hex32=%s" % (len(b), b[:32].hex())


def dump_map(idx):
    """返回 ([(key_bytes, val_bytes), ...], err)。用 -j 拿精确字节。"""
    p = PINROOT % idx
    out = subprocess.run(["bpftool", "-j", "map", "dump", "pinned", p],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
    try:
        j = json.loads(out)
    except Exception:
        return None, "pin %s: %s" % (p, out.decode("utf-8", "replace").strip()[:160])
    ents = []
    for e in j:
        ents.append((_to_bytes(e.get("key")), _to_bytes(e.get("value"))))
    return ents, ""


def _to_bytes(x):
    """bpftool -j 对同一字段有三种印法：单串 hex / ["74","63",...] / [116,99,...]。
    不归一化就会在"看着拿到了"的地方静默炸掉（本次首轮就是这么崩的）。"""
    if x is None:
        return b""
    if isinstance(x, str):
        return bytes.fromhex(x)
    out = bytearray()
    for it in x:
        out.append(int(it, 16) if isinstance(it, str) else int(it))
    return bytes(out)


def main():
    pid0, a0, loaded = 0, None, False
    head = pyread()
    target = head.split(b"\n")[0].split()[0].decode()
    print("[env] 加载前 /proc/modules 首行首词 = %r（总长 %d）" % (target, len(head)),
          flush=True)
    # ★ 推演里 ax_scan 会构造的那个键 —— 直接在这里算出来，S3 拿它逐字节比。
    want_key = target.encode() + b"\x00" * (64 - len(target))
    try:
        bin0 = e2e.gen_agent(LISTENER, "p26a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] aid=%s pid=%d" % (a0, pid0), flush=True)

        rec("S1 基线：加载前 Python 通道读到目标词", target.encode() in head, brief(head))

        # ── S2 加载 redact，names = [target, MARKER]（故意不加尾随 NUL）────────
        names = (target + "\x00" + MARKER).encode()
        txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", names, "", b"", PIN), timeout=90) or ""
        ok = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        rec("S2 redact 已加载 (names=[%s, MARKER])" % target, ok,
            (txt.strip().splitlines() or [""])[-1][:70])
        if not ok:
            return 1
        loaded = True
        time.sleep(1)

        # ── S3 map0 = hide_names：登记进去没有（**逐字节**比 ax_scan 要查的键）──
        ents, err = dump_map(0)
        if ents is None:
            rec("S3 map0(hide_names) 可读", False, err)
        else:
            keys = [k for k, _ in ents]
            rec("S3a map0 里有 %r 这个键（**逐字节**等于 ax_scan 构造的 64 字节键）"
                % target, want_key in keys,
                "%d 条键；%s" % (len(ents), "命中" if want_key in keys else "**未命中**"))
            if want_key not in keys:
                for k, _ in ents[:6]:
                    print("      键=%r" % k[:40], flush=True)
            rec("S3b 表里的键全部可解释（**报告项**，不是判据：多一条脏键 =
                意外遮蔽面）", True,
                " | ".join(repr(k.split(b'\\x00')[0]) for k, _ in ents))

        # ── S4 map7 = redact_fds：**fd 还开着**的时候 dump ────────────────────
        tgid = os.getpid()
        fd = os.open(MODULES, os.O_RDONLY)
        b4 = os.read(fd, 4096)
        time.sleep(0.3)
        fkey = (fd | (tgid << 32)).to_bytes(8, "little")
        ents7, err7 = dump_map(7)
        os.close(fd)
        if ents7 is None:
            rec("S4a map7(redact_fds) 可读", False, err7)
        else:
            hit = [v for k, v in ents7 if k == fkey]
            kind = hit[0][0] if hit and hit[0] else None
            rec("S4a ★★内核真相：fd 被记成 MODULES(kind=1)",
                kind == 1, "键(tgid=%d,fd=%d) → kind=%r（表里 %d 条）"
                           % (tgid, fd, kind, len(ents7)))
            if kind is None:
                for k, v in ents7[:6]:
                    print("      %s -> %r" % (k.hex(), v.hex()), flush=True)

        rec("S4b Python 通道（确定走 read(2)）：目标词被抹",
            target.encode() not in b4, brief(b4))

        b5 = catread()
        rec("S5 `cat` 通道（对手最常用的那条）：目标词被抹",
            target.encode() not in b5, brief(b5))
        rec("S6 ★ 两条通道结论一致（不同 ⇒ cat 不走 read 分支）",
            (target.encode() not in b4) == (target.encode() not in b5),
            "python抹=%s cat抹=%s" % (target.encode() not in b4,
                                      target.encode() not in b5))
        return 0 if all(R) else 1
    finally:
        if loaded:
            try:
                p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
            except Exception as e:
                print("[warn] unload: %s" % e, flush=True)
            time.sleep(0.8)
        after = pyread()
        print("[env] 复原：卸载后目标词又读得到 = %s" % (target.encode() in after),
              flush=True)
        p15.kill(pid0)
        print("\n[warn] 探针 = %d/%d" % (sum(R), len(R)), flush=True)
        print("RESULT: %s" % ("PASS" if R and all(R) else "FAIL"), flush=True)


if __name__ == "__main__":
    sys.exit(main())
