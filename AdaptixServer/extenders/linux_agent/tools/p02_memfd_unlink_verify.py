#!/usr/bin/env python3
"""P0-2 验收：memfd_exec --unlink（中转文件不落地残留）

args 用**逐字节复刻** matrix_resolver.go:264-272 的 beacon packed args，
所以这一跑同时验证了 Go 侧打包与 BOF 侧解析一致。

T1 对照：不带 --unlink → 源文件保留（证明 T2 的"消失"不是别的原因）
T2 带 --unlink → 源文件消失 **且**载荷副作用存在
   （ISS-020：任何"消失/为空"的断言都必须同时有"东西真的跑了"的证据）
T3 载荷副作用 → unlink 放在 memfd 写满之后，没有丢载荷（陷阱 6 的反面）
"""
import base64, os, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
from gap11_session_soak import _rows, max_id

BOF  = "/root/cc/run/extenders/linux_agent/bofs/memfd_exec.o"
SRC  = "/tmp/axm_v2"
MARK = "/tmp/p02_marker"
C    = ('#include <stdio.h>\n'
        'int main(void){ FILE*f=fopen("%s","w");'
        ' if(f){fputs("ran\\n",f);fclose(f);} return 0; }\n' % MARK)
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-44s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True, text=True).stdout


def pack_args(*toks):
    """复刻 matrix_resolver.go 的 beacon packed args：count + (len+token)*"""
    out = struct.pack("<I", len(toks))
    for t in toks:
        b = t.encode() if isinstance(t, str) else t
        out += struct.pack("<I", len(b)) + b
    return out


def run_bof(aid, bofname, argblob, timeout=120):
    base = max_id(aid)
    e2e.send_execute(aid, "execute bof %s.o" % bofname,
                     {"command": "execute", "subcommand": "bof",
                      "bof": BOF,
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
    open("/tmp/p02_payload.c", "w").write(C)
    cc = subprocess.run(["musl-gcc", "-static", "-Os", "-o", SRC, "/tmp/p02_payload.c"],
                        capture_output=True, text=True)
    if cc.returncode != 0:
        print("编译失败:", cc.stderr[:300])
        sys.exit(1)
    print("[p02] 载荷 %d B" % os.path.getsize(SRC), flush=True)

    binp = e2e.gen_agent("TestHTTP", "p02")
    pid  = e2e.launch(binp)
    aid  = e2e.wait_registered("TestHTTP", pid, timeout=90)
    print("[p02] agent id=%s pid=%d" % (aid, pid), flush=True)

    # ── T1 对照：不带 --unlink ──
    try:
        os.remove(MARK)
    except OSError:
        pass
    sh("cp %s /tmp/axm_v2c" % SRC)
    t1 = run_bof(aid, "memfd_exec", pack_args("/tmp/axm_v2c"))
    time.sleep(1)
    kept, ran1 = os.path.exists("/tmp/axm_v2c"), os.path.exists(MARK)
    rec("T1 对照：不带 --unlink → 源保留", kept and ran1,
        "src=%s marker=%s | %s" % (kept, ran1, (t1 or "").strip().replace("\n", " ")[:60]))

    # ── T2/T3 带 --unlink（矩阵链的真实布局）──
    try:
        os.remove(MARK)
    except OSError:
        pass
    t2 = run_bof(aid, "memfd_exec", pack_args(SRC, "--unlink"))
    time.sleep(1)
    gone, ran2 = not os.path.exists(SRC), os.path.exists(MARK)
    rec("T2 --unlink → 源消失且载荷已跑", gone and ran2,
        "src_exists=%s marker=%s | %s" % (not gone, ran2,
                                          (t2 or "").strip().replace("\n", " ")[:60]))
    rec("T3 副作用内容正确（unlink 没丢载荷）", ran2,
        open(MARK).read().strip() if ran2 else "marker 缺失")

    e2e.kill(pid)
    sh("rm -f /tmp/axm_v2c %s /tmp/p02_payload.c" % SRC)
    print("\nPASS=%d FAIL=%d" % (R.count(True), R.count(False)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    main()
