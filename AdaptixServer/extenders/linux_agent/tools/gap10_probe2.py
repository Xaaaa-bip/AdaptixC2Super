#!/usr/bin/env python3
"""gap10 探针2：ebpf_load 单发 + 逐秒观测 map/prog/pin/tick，定位卡点。"""
import base64, json, os, sqlite3, struct, subprocess, sys, time
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e

BOF = "/root/cc/run/extenders/linux_agent/bofs"
OUT = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bpf/out"
BIN = "/tmp/e2e_work/e2e_TestHTTP"


def sh(c):
    return subprocess.run(c, shell=True, capture_output=True, text=True).stdout.strip()


def cnt(what):
    out = sh("bpftool %s list 2>/dev/null" % what)
    return len([l for l in out.splitlines() if l and l[0].isdigit()])


def pins():
    return sh("ls /sys/fs/bpf 2>/dev/null").split()


def cmax():
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    m = con.execute("SELECT COALESCE(MAX(Id),0) FROM Consoles").fetchone()[0]
    con.close(); return m


def rows(aid, mn):
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    r = con.execute("SELECT CAST(Packet AS TEXT) FROM Consoles WHERE AgentId=? AND Id>? ORDER BY Id",
                    (aid, mn)).fetchall()
    con.close(); return [x[0] for x in r]


def tick(aid):
    try:
        for a in e2e._auth_http("GET", "/agent/list"):
            if a.get("a_id") == aid:
                return time.time() - a.get("a_last_tick", 0)
    except Exception:
        pass
    return None


def sfield(s):
    b = s.encode() if isinstance(s, str) else s
    return struct.pack("<I", len(b)) + b


def pack(prog_type, attach, target, names="", ports="", key=b"", pin=""):
    return (struct.pack("<I", 9) + sfield("%s/hide.insns.bin" % OUT) +
            sfield("%s/hide.mapreloc.txt" % OUT) +
            struct.pack("<i", prog_type) + struct.pack("<i", attach) +
            sfield(target) + sfield(names) + sfield(ports) + sfield(key) + sfield(pin))


def send(aid, blob, tag):
    e2e.send_execute(aid, "execute bof ebpf_load.o",
                     {"command": "execute", "subcommand": "bof",
                      "bof": "%s/ebpf_load.o" % BOF,
                      "param_data": base64.b64encode(blob).decode()})
    print("[probe2] sent %s" % tag, flush=True)


pid = e2e.launch(BIN)
aid = e2e.wait_registered("TestHTTP", pid, timeout=60)
print("[probe2] aid=%s pid=%s  maps=%d progs=%d pins=%d" %
      (aid, pid, cnt("map"), cnt("prog"), len(pins())), flush=True)


def watch(aid, mn, secs, tag):
    t0 = time.time()
    last = None
    while time.time() - t0 < secs:
        t = tick(aid)
        m, p, pn = cnt("map"), cnt("prog"), len(pins())
        cur = (m, p, pn)
        rs = rows(aid, mn)
        line = "  t=%4.0fs tick=%-6s maps=%d progs=%d pins=%d rows=%d" % (
            time.time() - t0, ("%.1f" % t if t is not None else "GONE"), m, p, pn, len(rs))
        if cur != last or rs:
            print(line, flush=True)
            for x in rs[-2:]:
                d = json.loads(x)
                print("        -> done=%s %r %r" % (d.get("a_completed"),
                                                    (d.get("a_text") or "")[:150],
                                                    (d.get("a_message") or "")[:80]), flush=True)
        last = cur
        if rs and all(json.loads(x).get("a_completed") for x in rs):
            print("  ===== 完成 =====", flush=True)
            return True
        time.sleep(2)
    print("  ----- 超时未完成 -----", flush=True)
    return False


# 步骤1：不 attach，只建图+改重定位+PROG_LOAD+pin
b = cmax()
send(aid, pack(5, 0, "", pin="axp1"), "step1 attach=0 pin=axp1")
watch(aid, b, 46, "step1")
print("[probe2] after step1: maps=%d progs=%d pins=%s" % (cnt("map"), cnt("prog"), pins()), flush=True)
print("[probe2] tick_age=%s" % tick(aid), flush=True)

os.kill(pid, 9)
for p in pins():
    if p.startswith("axp1"):
        sh("rm -f /sys/fs/bpf/%s" % p)
print("[probe2] done")
