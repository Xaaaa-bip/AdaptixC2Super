#!/usr/bin/env python3
"""gap10 探针：区分「任务没投递」与「BOF 挂死」。复用缓存 payload，秒级出结果。"""
import base64, json, os, sqlite3, struct, sys, time
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e

BOF = "/root/cc/run/extenders/linux_agent/bofs"
BIN = "/tmp/e2e_work/e2e_TestHTTP"


def cmax():
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    m = con.execute("SELECT COALESCE(MAX(Id),0) FROM Consoles").fetchone()[0]
    con.close(); return m


def rows(aid, mn):
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    r = con.execute("SELECT Id, CAST(Packet AS TEXT) FROM Consoles WHERE AgentId=? AND Id>? ORDER BY Id",
                    (aid, mn)).fetchall()
    con.close(); return r


def show(aid, mn, tag):
    rs = rows(aid, mn)
    print("  --%s: %d rows" % (tag, len(rs)))
    for i, t in rs:
        d = json.loads(t)
        print("     [%s] done=%s text=%r msg=%r" % (d.get("type"), d.get("a_completed"),
                                                     (d.get("a_text") or "")[:120],
                                                     (d.get("a_message") or "")[:60]))
    return rs


def alive(aid):
    try:
        ags = e2e._auth_http("GET", "/agent/list")
        for a in ags:
            if a.get("a_id") == aid:
                return time.time() - a.get("a_last_tick", 0)
    except Exception as e:
        return "err %s" % e
    return None


def sfield(s):
    b = s.encode(); return struct.pack("<I", len(b)) + b


print("[probe] launch", flush=True)
pid = e2e.launch(BIN)
aid = e2e.wait_registered("TestHTTP", pid, timeout=60)
print("[probe] aid=%s pid=%s" % (aid, pid), flush=True)

b0 = cmax()
e2e.send_raw(aid, "getuid")
print("[probe] send_raw getuid sent; tick_age=%s" % alive(aid), flush=True)
time.sleep(20)
show(aid, b0, "raw/getuid")
print("[probe] tick_age=%s" % alive(aid), flush=True)

b1 = cmax()
blob = struct.pack("<I", 1) + sfield("/etc/hostname")
e2e.send_execute(aid, "execute bof host_recon.o",
                 {"command": "execute", "subcommand": "bof",
                  "bof": "%s/host_recon.o" % BOF,
                  "param_data": base64.b64encode(blob).decode()})
print("[probe] send_execute bof sent", flush=True)
time.sleep(30)
show(aid, b1, "execute/bof")
print("[probe] tick_age=%s (None=掉线)" % alive(aid), flush=True)

os.kill(pid, 9)
print("[probe] done")
