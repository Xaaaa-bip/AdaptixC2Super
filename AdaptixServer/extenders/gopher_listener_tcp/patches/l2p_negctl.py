#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 批次16：relay 承重性的阴性对照 + agent 实际路径直证。

判据（全部来自可观测事实，非回显、非日志转述）：
  N0  relay 重启后 NODE-PEER 与停机前一致（身份持久 ⇒ 监听器电路地址不失配）
  N1  relay 停机时：同一 agent 二进制在窗口内 **不注册**（阴性 ⇒ 无直连旁路）
  N2  relay 停机时：该 agent 进程连接表里没有到 relay:4446 的 ESTAB
  N3  relay 起回后：同一二进制在窗口内注册（阳性 ⇒ relay 是承载而不是装饰）
  N4  注册后：agent 进程的连接 **只有** → relay:4446，**没有** → C2:<listener端口>
      ⇒ C2 真实 IP:端口不进 agent（这才是"批次5 藏 C2"的判据；l_agent_addr 不是）
  N5  relay 进程同时持有 到 agent 与 到 C2 的两条 ESTAB（两腿都在 relay 里）

用法: python3 -B l2p_negctl.py [--port 39012] [--window 60]
"""
import argparse, json, os, re, subprocess, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, "/root/gp5probe")
import l2papi as E  # noqa: E402

BIN = "/root/gp5probe/tmp/e2e/agent_l2p3.bin"
RELAY_PORT = 4446
RELAY_DIR = "/root/gp5probe"
RELAY_BIN = "./axrelay"
RELAY_ARGS = ["-listen", "/ip4/127.0.0.1/tcp/%d" % RELAY_PORT,
              "-key", "/root/gp5probe/relaynode.key"]
RELAY_LOG = "/root/gp5probe/axrelay.negctl.log"
PEER_EXPECT = "12D3KooWM5cNF3ozC3FwdmP7LYZeR823Lc4nEADPRkR2vHmqiYSF"
USER, PW = "central", "qaxnb@666crazydocT0r!"
OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"
fails = []


def judge(cond, msg):
    print(("%s %s" % (OK if cond else BAD, msg)))
    if not cond:
        fails.append(msg)
    return cond


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout


def login():
    st, b = E.call("/login", {"username": USER, "password": PW,
                              "only_password": True})
    if st != 200:
        print("%s 登录失败 %s %s" % (BAD, st, b[:200]))
        sys.exit(2)
    return json.loads(b)["access_token"]


def agents(tok):
    st, b = E.call("/agent/list", None, tok, "GET")
    if st != 200:
        print("%s /agent/list -> %s %s（判据不可判 ⇒ 按失败处理）" % (BAD, st, b[:120]))
        return None
    return {a.get("a_id"): a for a in json.loads(b)}


def port_owner(port):
    """(pid, name) —— 端口归属即事实，不按进程名 kill。"""
    out = sh("ss -ltnp 2>/dev/null | grep -F ':%d '" % port)
    m = re.search(r'users:\(\("([^"]+)",pid=(\d+)', out)
    return (int(m.group(2)), m.group(1)) if m else (None, None)


def conns(pid):
    out = sh("ss -tnp 2>/dev/null | grep -F 'pid=%d,'" % pid)
    return [ln.split() for ln in out.strip().splitlines() if ln.strip()]


def start_agent():
    log = open("/root/gp5probe/tmp/e2e/agent_l2p3.negctl.log", "w")
    p = subprocess.Popen([BIN], cwd="/root/gp5probe/tmp/e2e", stdout=log,
                         stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                         start_new_session=True)
    return p.pid


def wait_agent(tok, base, win, tag):
    """窗口内轮询新 a_id；返回 (新a_id or None, 首见时刻秒)"""
    t0 = time.time()
    while time.time() - t0 < win:
        time.sleep(3)
        cur = agents(tok)
        if cur is None:
            return None, -1
        new = [k for k in cur if k not in base]
        if new:
            return new[0], time.time() - t0
    return None, -1


def relay_peer(logpath):
    try:
        with open(logpath, errors="replace") as f:
            for ln in f:
                if ln.startswith("NODE-PEER"):
                    return ln.split(None, 1)[1].strip()
    except Exception:
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=39012)
    ap.add_argument("--window", type=int, default=60)
    ap.add_argument("--positive-only", action="store_true",
                    help="跳过停/起 relay，只跑阳性腿：用于 relay 与预约都在位的复测")
    a = ap.parse_args()
    lp = a.port + 5  # l2p3 的 listener 端口 = 39017
    print("%s rig=%d listener端口=%d relay=%d bin=%s" % (INFO, a.port, lp, RELAY_PORT, BIN))
    tok = login()
    base = agents(tok)
    if base is None:
        return 2
    print("%s 基线 agent 数=%d" % (INFO, len(base)))
    old_agents = set(base)

    if a.positive_only:
        npid, nname = port_owner(RELAY_PORT)
        judge(nname == "axrelay", "N/前置：relay 在跑（pid=%s）" % npid)
        apid = start_agent()
        new2, dt3 = wait_agent(tok, old_agents, a.window, "N3")
        judge(new2 is not None, "N3 relay 在跑 ⇒ %.0fs 内注册（阳性成立）" % dt3)
        if not new2:
            print("%s 阳性腿失败 ⇒ 现场 agent pid=%s 连接=%s" % (BAD, apid, conns(apid)))
            print("\nRESULT: 1 项失败: 阳性腿不成立")
            return 1
        rec = agents(tok)[new2]
        print("%s a_id=%s a_pid=%s a_session_key=%s" %
              (INFO, new2, rec.get("a_pid"), (rec.get("a_session_key") or "")[:12]))
        time.sleep(3)
        cs2 = conns(apid)
        r2 = [c for c in cs2 if ":%d" % RELAY_PORT in c[3:5]]
        c2 = [c for c in cs2 if ":%d" % lp in c[3:5]]
        print("%s agent pid=%d 连接：%s" % (INFO, apid, cs2))
        judge(bool(r2), "N4a agent 有到 relay:4446 的 ESTAB")
        judge(not c2, "N4b agent **没有**到 C2:%d 的连接 ⇒ C2 真实地址不进 agent" % lp)
        legs = sh("ss -tnp 2>/dev/null | grep -F 'pid=%s,'" % npid)
        judge((":%d" % RELAY_PORT) in legs and (":%d" % lp) in legs,
              "N5 relay 进程同时持有 到agent/到C2 两条腿（承重）")
        print("%s relay 腿：\n%s" % (INFO, legs.strip()))
        print("\nRESULT: %s" % ("全部判据绿" if not fails else "%d 项失败: %s" % (len(fails), fails)))
        return 1 if fails else 0

    # ---- 停机 ----
    rpid, rname = port_owner(RELAY_PORT)
    print("%s 4446 归属 pid=%s name=%s" % (INFO, rpid, rname))
    judge(rpid is not None and rname == "axrelay", "N/前置：4446 由 axrelay 持有")
    peer_before = relay_peer("/root/gp5probe/axrelay.3.log")
    if rpid:
        os.kill(rpid, 15)
    for _ in range(10):
        time.sleep(1)
        if port_owner(RELAY_PORT)[0] is None:
            break
    if port_owner(RELAY_PORT)[0] is not None:   # SIGTERM 无效则升级
        print("%s SIGTERM 未生效，升级 SIGKILL" % INFO)
        os.kill(rpid, 9)
        time.sleep(3)
    judge(port_owner(RELAY_PORT)[0] is None, "N/前置：relay 已停（4446 无监听）")

    # ---- N1/N2 阴性 ----
    apid = start_agent()
    time.sleep(4)
    cs = conns(apid)
    to_relay = [c for c in cs if ":%d" % RELAY_PORT in c[3:5]]
    to_c2 = [c for c in cs if ":%d" % lp in c[3:5]]
    print("%s relay 停机期 agent pid=%d 连接数=%d 到relay=%d 到C2直连=%d"
          % (INFO, apid, len(cs), len(to_relay), len(to_c2)))
    judge(not to_relay, "N2 relay 停机 ⇒ agent 无到 4446 的 ESTAB")
    new, dt = wait_agent(tok, old_agents, a.window, "N1")
    judge(new is None, "N1 relay 停机 ⇒ %.0fs 窗口内不注册（阴性成立）" % a.window)
    if new:
        print("%s 意外注册 a_id=%s 于 %.0fs ⇒ 存在直连旁路！" % (BAD, new, dt))
        print("%s 该实例连接表：%s" % (INFO, conns(apid)))
    extra, dt2 = wait_agent(tok, old_agents, 30, "N1b")
    judge(extra is None, "N1b 后窗 30s 复核：仍不注册")
    sh("kill -9 %d 2>/dev/null" % apid)

    # ---- 起回 ----
    log = open(RELAY_LOG, "w")
    subprocess.Popen([RELAY_BIN] + RELAY_ARGS, cwd=RELAY_DIR, stdout=log,
                     stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                     start_new_session=True)
    for _ in range(20):
        time.sleep(1)
        if port_owner(RELAY_PORT)[0] is not None:
            break
    npid, nname = port_owner(RELAY_PORT)
    judge(nname == "axrelay", "N/前置：relay 已起回（pid=%s）" % npid)
    peer_after = relay_peer(RELAY_LOG)
    print("%s NODE-PEER before=%s after=%s" % (INFO, peer_before, peer_after))
    judge(peer_after == PEER_EXPECT and peer_before == peer_after,
          "N0 relay 身份持久（peer id 与停机前一致）")

    # ---- N3/N4/N5 阳性 ----
    apid2 = start_agent()
    new2, dt3 = wait_agent(tok, old_agents, a.window, "N3")
    judge(new2 is not None, "N3 relay 起回 ⇒ %.0fs 内注册（阳性成立）" % dt3)
    if new2:
        rec = agents(tok)[new2]
        print("%s a_id=%s a_pid=%s a_session_key=%s" %
              (INFO, new2, rec.get("a_pid"), (rec.get("a_session_key") or "")[:12]))
        time.sleep(3)
        cs2 = conns(apid2)
        r2 = [c for c in cs2 if ":%d" % RELAY_PORT in c[3:5]]
        c2 = [c for c in cs2 if ":%d" % lp in c[3:5]]
        print("%s agent pid=%d 连接：%s" % (INFO, apid2, cs2))
        judge(bool(r2), "N4a agent 有到 relay:4446 的 ESTAB")
        judge(not c2, "N4b agent **没有**到 C2:%d 的连接 ⇒ C2 真实地址不进 agent" % lp)
        legs = sh("ss -tnp 2>/dev/null | grep -F 'pid=%s,'" % npid)
        judge((":%d" % RELAY_PORT) in legs and (":%d" % lp) in legs,
              "N5 relay 进程同时持有 到agent/到C2 两条腿（承重）")
        print("%s relay 腿：\n%s" % (INFO, legs.strip()))
    else:
        print("%s 起回后仍未注册 ⇒ relay 相关判据全部不可判" % BAD)

    print("\nRESULT: %s" % ("全部判据绿" if not fails
                            else "%d 项失败: %s" % (len(fails), fails)))
    print("%s 现场保留：relay pid=%s agent pid=%s" % (INFO, npid, apid2))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
