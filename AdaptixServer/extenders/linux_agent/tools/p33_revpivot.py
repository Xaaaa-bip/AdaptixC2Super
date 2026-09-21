#!/usr/bin/env python3
"""p33_revpivot.py — 反向 pivot（子 agent 主动拨父）端到端判据。

背景
----
"内网跳板"要能落地，子机侧常常**入站不可达**（主机防火墙默认 deny、NAT 后面、
没有可绑的端口）。正向 pivot 要求子机开放监听 ⇒ 在那种环境里必然失败。
反向 pivot 把方向倒过来：**父 agent 监听，子 agent 主动拨出去**，子机零监听端口、
只需要"出站可达"。

判据的骨架是 **A/B 对照，变量只有一个**：同一端口、同一生成流程，
唯一差别是 listener 配置里有没有 `callback_addresses`。

  P1 红对照 `P33RevOff`：无 callback_addresses ⇒ 子 agent **必须**在 42002 监听
  P2 绿     `P33RevOn` ：有 callback_addresses ⇒ 子 agent **必须**零监听、拨入父

P1 存在的意义就是证明"监听"这件事**看得见** —— 否则 P2 的"没看见监听"
分不清是"反向成立"还是"这台机器上根本测不出监听"。

用法
----
  p33_revpivot.py verify [--port 42002] [--timeout 120]

退出码：0 全绿 / 1 有判据不成立 / 2 环境不满足（不打分，避免假红）
"""
import json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e

RIG = "/root/gp5probe/revpivot_rig"
WORK = "/root/gp5probe/p33work"
PARENT_LST = "P33Parent"      # GopherTCP @42000，反向 pivot 的父
_LOGS = []
_RESULT = []


# ───────────────────────── rig 接线 ─────────────────────────

def setup():
    e2e.API = "https://127.0.0.1:38312/api"
    e2e.DB = RIG + "/data/adaptixserver.db"
    e2e.TOK_FILE = WORK + "/token.txt"
    e2e.WORK = WORK
    # 口令从 rig 的 profile 现取，不在脚本里硬编码第二份
    txt = open(RIG + "/central.profile", "rb").read().decode("utf-8", "replace")
    m = re.search(r'password\s*:\s*["\']?([^"\'\s]+)', txt)
    if m:
        e2e.PASS = m.group(1)
    # 日志落 rig 工作区（不碰 /tmp：那是 p17 的观测台面）
    e2e.launch = _launch


def _launch(bin_path, tag="rev"):
    os.makedirs(WORK, exist_ok=True)
    path = "%s/agent_%s_%d_%d.log" % (WORK, tag, os.getpid(), len(_LOGS) + 1)
    out = open(path, "wb", buffering=0)
    _LOGS.append(out)
    e2e.LAST_AGENT_LOG = path
    return subprocess.Popen([bin_path], stdout=out, stderr=out).pid


# ───────────────────────── 观测原语 ─────────────────────────

def ss_listen():
    """本机所有监听端口 -> {port: (addr, pid_or_None)}。"""
    out = subprocess.run(["ss", "-ltnpH"], capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) < 4:
            continue
        addr = f[3]
        port = int(addr.rsplit(":", 1)[-1])
        pids = re.findall(r"pid=(\d+)", line)
        res[port] = (addr, int(pids[0]) if pids else None)
    return res


_ADDRPORT = re.compile(r"^[0-9A-Fa-f:\.\*\[\]%]+:\d+$")


def ss_established(port):
    """port 上的 ESTABLISHED 连接 -> [(本地端, 对端, [pids])]。

    ★ 不能按位置切片：`ss -H` 配合 `state` 过滤时会**省掉 State 列**，
      字段数在 5/6 之间浮动 ⇒ f[3]/f[4] 会指错列（实测把"父持有已建立连接"
      误判成"没有连接" —— 判据假红）。
      改为按 addr:port 形状正则取前两个匹配 = Local, Peer，与列数无关。
    """
    out = subprocess.run(["ss", "-tnpH", "state", "established"],
                         capture_output=True, text=True).stdout
    res = []
    for line in out.splitlines():
        toks = [t for t in line.split() if _ADDRPORT.match(t)]
        if len(toks) < 2:
            continue
        loc, peer = toks[0], toks[1]
        if loc.rsplit(":", 1)[-1] != str(port) and peer.rsplit(":", 1)[-1] != str(port):
            continue
        res.append((loc, peer, [int(p) for p in re.findall(r"pid=(\d+)", line)]))
    return res


def agents():
    r = e2e._auth_http("GET", "/agent/list")
    return r if isinstance(r, list) else []


def agent_ids():
    return {a.get("a_id") for a in agents()}


def proc_alive(pid):
    return os.path.isdir("/proc/%d" % pid)


def wait_new_agent(before, timeout):
    """等一个不在 before 里的新 agent id 出现（子 agent 的 a_listener 名字不可预知，
    所以按"新增"而不是"按名字"找）。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        for a in agents():
            if a.get("a_id") not in before and time.time() - a.get("a_last_tick", 0) < 30:
                return a
        time.sleep(1)
    return None


def wait_port(port, want, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if (port in ss_listen()) == want:
            return True
        time.sleep(0.5)
    return False


def mklistener(name, port, cbaddrs=None):
    """幂等创建：已存在且配置一致就直接用（listener 无 delete 端点，只能复用）。"""
    cfg = {"port_bind": port, "encrypt_key": "0123456789abcdef0123456789abcdef"}
    if cbaddrs:
        cfg["callback_addresses"] = cbaddrs
    r = e2e._auth_http("POST", "/listener/create",
                       {"name": name, "type": "LinuxTCP", "config": json.dumps(cfg)})
    if r.get("ok"):
        return r
    cur = readback(name)
    if cur:
        got = json.loads(cur)
        if got.get("port_bind") == port and (got.get("callback_addresses") or None) == (cbaddrs or None):
            print("  [=] %s 已存在且配置一致，复用" % name, flush=True)
            return r
        raise RuntimeError("%s 已存在但配置不一致: %s" % (name, cur))
    raise RuntimeError("create %s failed: %r" % (name, r))


def readback(name):
    """从 API 读回该 listener 的 l_data -- 用于证明 callback_addresses 真进了 core。"""
    for l in e2e._auth_http("GET", "/listener/list"):
        if l.get("l_name") == name:
            return l.get("l_data")
    return None


# ───────────────────────── 判据 ─────────────────────────

def check(tag, ok, detail):
    _RESULT.append((tag, ok, detail))
    print("  [%s] %-4s %s" % ("V" if ok else "X", tag, detail), flush=True)
    return ok


def phase_red(port, timeout):
    """P1 红对照：无 callback_addresses ⇒ 子 agent 必须在 <port> 监听。"""
    print("\n=== P1 红对照 P33RevOff (无 callback_addresses) ===", flush=True)
    mklistener("P33RevOff", port)
    ldata = readback("P33RevOff")
    check("R0", ldata is not None and "callback_addresses" not in (ldata or ""),
          "l_data 无 callback_addresses: %s" % (ldata or "")[:90])

    before = agent_ids()
    binp = e2e.gen_agent("P33RevOff")
    pid = e2e.launch(binp, "red")
    try:
        listening = wait_port(port, True, timeout)
        check("R1", listening, "子 agent 监听 %d: %s" % (port, ss_listen().get(port)))
        # ★ 红对照**不**断言"注册"：internal(bind) 形态是 agent 被动等人拨进来，
        #   没有 dialer 就永远不会注册。断言它存活 = 它确实停在 accept 上
        #   （而不是走别的路退出）——这正是 P2 要对照的那个"看得见的监听"。
        check("R2", proc_alive(pid), "子 agent 存活于 accept 等待 (pid=%d)" % pid)
    finally:
        e2e.kill(pid)
        wait_port(port, False, 20)


def phase_green(port, timeout):
    """P2 绿：callback_addresses 指向父在听的端口 ⇒ 子零监听 + 拨入 + 数据面通。"""
    print("\n=== P2 绿 P33RevOn (callback_addresses=[127.0.0.1:%d]) ===" % port, flush=True)
    mklistener("P33RevOn", port, ["127.0.0.1:%d" % port])
    ldata = readback("P33RevOn")
    check("G0", "callback_addresses" in (ldata or ""),
          "l_data 含 callback_addresses: %s" % (ldata or "")[:110])

    # 父 agent：普通 GopherTCP agent，收到 `listen <port>` 后开始监听
    pbin = e2e.gen_agent(PARENT_LST, "parent")
    ppid = e2e.launch(pbin, "parent")
    try:
        paid = e2e.wait_registered(PARENT_LST, ppid, timeout)
        check("G1", bool(paid), "父 agent 注册 aid=%s pid=%d" % (paid, ppid))

        # ★ 先确认监听没起来，再下发 listen，避免把"本来就在听"当成"listen 生效"
        check("G2", port not in ss_listen(), "%d 下发前未被占用" % port)
        e2e.send_raw(paid, "listen %d" % port)
        check("G3", wait_port(port, True, timeout),
              "父收到 listen 后在听 %d: %s" % (port, ss_listen().get(port)))

        # 子 agent
        before = agent_ids()
        listen_before = ss_listen()
        cbin = e2e.gen_agent("P33RevOn", "child")
        cpid = e2e.launch(cbin, "child")
        try:
            # ★ N2：子进程零新增监听端口
            time.sleep(6)
            after = ss_listen()
            new_ports = {p: v for p, v in after.items() if p not in listen_before}
            check("N2", not new_ports,
                  "子 agent 零新增监听端口 (新增=%s)" % (new_ports or "无"))

            # ★ N3：42002 上存在 ESTABLISHED，且**父进程持有 listen 侧那一端**
            #   （断言"父持有本地端"而不是"父出现在 pid 列表里"：后者对端也算，
            #    分不出谁在听、谁在拨）
            est = ss_established(port)
            pids = sorted({p for _, _, ps in est for p in ps})
            parent_side = [(loc, peer) for loc, peer, ps in est
                           if ppid in ps and loc.rsplit(":", 1)[-1] == str(port)]
            check("N3", len(parent_side) >= 1,
                  "父持有 %d 的 listen 侧 ESTABLISHED: %s (全部=%s, pid=%s, 父=%d, 子=%d)" %
                  (port, parent_side, est, pids, ppid, cpid))

            # ★ N4：teamserver 出现新 agent
            a = wait_new_agent(before, timeout)
            check("N4", a is not None,
                  "新 agent 注册: %s listener=%s pid=%s" %
                  (a.get("a_id") if a else None,
                   a.get("a_listener") if a else None,
                   a.get("a_pid") if a else None))

            # ★ N5：数据面 —— 对新 agent 下 getuid
            if a:
                e2e.send_raw(a["a_id"], "getuid")
                msg = e2e.console_poll(a["a_id"], '"a_completed":true', timeout)
                text = json.loads(msg).get("a_text", "")
                check("N5", "uid=0" in text, "数据面 getuid -> %r" % text[:70])
            else:
                check("N5", False, "无新 agent，跳过数据面")

            # ★ N6：子 agent 本体仍在（没走监听兜底退出那条路）
            check("N6", proc_alive(cpid), "子 agent 进程存活 pid=%d" % cpid)
        finally:
            e2e.kill(cpid)
        e2e.send_raw(paid, "listen 0")
        check("G4", wait_port(port, False, timeout), "listen 0 后 %d 已关闭" % port)
    finally:
        e2e.kill(ppid)


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    opt = lambda n, d: args[args.index(n) + 1] if n in args else d
    port = int(opt("--port", 42002))
    timeout = int(opt("--timeout", 120))

    setup()
    print("rig=%s  port=%d  timeout=%ds" % (RIG, port, timeout), flush=True)

    # 环境前提：rig 在跑；端口要么空闲、要么被 rig 自己占（不算前提失败）
    if not os.path.exists(RIG + "/rig.log"):
        print("ENV: rig 目录不存在"); return 2
    if "The AdaptixC2 server is ready" not in open(RIG + "/rig.log", errors="replace").read()[-4000:]:
        print("ENV: rig 未就绪"); return 2
    try:
        e2e._force_token()
    except Exception as ex:
        print("ENV: rig 登录失败: %s" % ex); return 2
    if port in ss_listen():
        print("ENV: 端口 %d 已被占用 %s" % (port, ss_listen()[port])); return 2

    try:
        phase_red(port, timeout)
        phase_green(port, timeout)
    except Exception as ex:
        import traceback; traceback.print_exc()
        _RESULT.append(("EXC", False, str(ex)))

    ok = sum(1 for _, o, _ in _RESULT if o)
    print("\n判据 = %d/%d" % (ok, len(_RESULT)), flush=True)
    for t, o, d in _RESULT:
        if not o:
            print("  未成立 %s: %s" % (t, d), flush=True)
    print("RESULT: %s" % ("PASS" if ok == len(_RESULT) and _RESULT else "FAIL"), flush=True)
    return 0 if ok == len(_RESULT) and _RESULT else 1


if __name__ == "__main__":
    sys.exit(main())
