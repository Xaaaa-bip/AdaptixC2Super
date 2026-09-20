#!/usr/bin/env python3
"""e2e.py — linux_agent 端到端验证框架（替代手工 curl/看日志/抓包）。

用法:
  e2e.py roundtrip --listener TestHTTP [--cmd getuid] [--timeout 90] [--keep]
  e2e.py all [--listeners TestHTTP,TestWS,TestHTTPS,TestWSS,gopher] [--timeout 90]
  e2e.py matrix --listener TestHTTP [--timeout 120]   # Phase 3 gate: check→解析→推送→执行全自动链

断言方式: 登录→生成→启动→轮询 /agent/list 按 pid 确认注册→发命令→
轮询 sqlite Consoles 表断言 type=107 完成消息。任何一步超时即 FAIL。
"""
import base64, json, os, signal, sqlite3, ssl, subprocess, sys, time, urllib.request

API = "https://127.0.0.1:38212/api"
PASS = "qaxnb@666crazydocT0r!"
DB = "/root/cc/run/data/adaptixserver.db"
TOK_FILE = "/tmp/e2e_token.txt"
WORK = "/tmp/e2e_work"
AGENT_CFG = {"format": "Binary ELF (Native)", "arch": "x86_64",
             "reconn_timeout": "5", "reconn_count": 100, "opsec_enabled": False}

_ctx = ssl._create_unverified_context()

# ★★ agent 尸检材料（本批 p20 的学费，见 launch() 头注）。
#   持有句柄是为了不让 GC 把它们关掉 —— 关掉之后子进程再写就是 EBADF，
#   日志会静默变成空文件，比没有日志更坏（"有文件但没内容"会被当成"没报错"）。
_AGENT_LOGS = []
_AGENT_N = 0
LAST_AGENT_LOG = None

def _http(method, path, body=None, tok=None):
    req = urllib.request.Request(API + path, method=method)
    if tok:
        req.add_header("Authorization", "Bearer " + tok)
    data = None if body is None else json.dumps(body).encode()
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, data=data, context=_ctx, timeout=300) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return {"_http_error": e.code, "body": e.read().decode()[:120]}

def _force_token():
    try:
        os.remove(TOK_FILE)
    except OSError:
        pass
    return _token()

def _auth_http(method, path, body=None):
    """带 token 的请求；服务器重启会换 JWT 密钥（auth 失败伪装 404），
    遇 404 强制重登录重试一次。"""
    r = _http(method, path, body, tok=_token())
    if isinstance(r, dict) and "_http_error" in r:
        r = _http(method, path, body, tok=_force_token())
    return r

def _token():
    try:
        tok, exp = open(TOK_FILE).read().strip().split("|")
        if float(exp) > time.time() + 120:
            return tok
    except Exception:
        pass
    r = _http("POST", "/login", {"password": PASS})
    if "access_token" not in r:
        raise RuntimeError("login failed: %r" % r)
    tok = r["access_token"]
    payload = tok.split(".")[1]
    payload += "=" * (-len(payload) % 4)
    exp = json.loads(base64.urlsafe_b64decode(payload))["exp"]
    with open(TOK_FILE, "w") as f:
        f.write("%s|%s" % (tok, exp))
    return tok

def gen_agent(listener, name="e2e"):
    os.makedirs(WORK, exist_ok=True)
    tok = _token()
    r = _auth_http("POST", "/agent/generate",
              {"listener_name": [listener], "agent": "linux",
               "config": json.dumps(AGENT_CFG)})
    if not r.get("ok"):
        raise RuntimeError("generate %s failed: %r" % (listener, r))
    fname_b64, content_b64 = r["message"].split(":")
    path = os.path.join(WORK, "%s_%s" % (name, listener))
    # 写临时文件再原子换名：目标路径可能正被上一轮 agent 当作执行映像占用，
    # 直接 open(path,"wb") 会 ETXTBSY（ISS-027）；rename 不受该限制。
    tmp = "%s.new.%d" % (path, os.getpid())
    with open(tmp, "wb") as f:
        f.write(base64.b64decode(content_b64))
    os.chmod(tmp, 0o755)
    os.replace(tmp, path)
    return path

def launch(bin_path, tag=""):
    """启动 agent，**并把它的 stdout/stderr 留档**（路径见 e2e.LAST_AGENT_LOG）。

    ★ 为什么必须留（p20 T5 交的学费）：原来两路都进 DEVNULL，于是
    「agent 卡死」「agent 已死」「agent 活着但没回执」三种**完全不同**的状态
    在台面上**完全同形** —— 都是"没有回执"。实测那次：T5b 三次重试留下 3 个
    孤立 type=106、dmesg 干净（无 traps:）、API 全部 ok，光看台面判不出任何东西，
    最后是靠 Agents.LastTick 才反推出"agent 早在第一次重试之前就停止心跳了"。
    stderr 通常只有一两行，但那一两行就是**全部**的尸检材料（glibc 的 malloc
    断言、assert 失败、自毁前的最后一句）。

    落点用 `/tmp/reg_agent_*.log`：它落在 p17 已豁免的 `reg…(out|log)` **命名族**
    里（见 p17 头注"ISS-066 的第二个实例：漏的不是路径而是命名族"），所以不会
    引入新的假红。打不开就退回 DEVNULL —— **观测手段不能反过来把台面弄红**。
    """
    global _AGENT_N, LAST_AGENT_LOG
    out = subprocess.DEVNULL
    try:
        _AGENT_N += 1
        path = "/tmp/reg_agent_%s%d_%d.log" % (tag, os.getpid(), _AGENT_N)
        out = open(path, "wb", buffering=0)
        _AGENT_LOGS.append(out)
        LAST_AGENT_LOG = path
    except Exception:
        out = subprocess.DEVNULL
    return subprocess.Popen([bin_path], stdout=out, stderr=out).pid

def kill(pid):
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass

def wait_registered(listener, pid, timeout=60):
    tok = _token()
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            agents = _auth_http("GET", "/agent/list")
        except Exception:
            agents = []
        if isinstance(agents, list):
            for a in agents:
                if (a.get("a_listener") == listener and
                        str(a.get("a_pid")) == str(pid) and
                        time.time() - a.get("a_last_tick", 0) < 30):
                    return a["a_id"]
        time.sleep(1)
    raise TimeoutError("agent on %s (pid %s) not registered" % (listener, pid))

def wait_registered_latest(listener, timeout=60):
    """返回 listener 上 tick 最新的 agent id（pid 未知时用）。"""
    tok = _token()
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            agents = _auth_http("GET", "/agent/list")
        except Exception:
            agents = []
        if isinstance(agents, list):
            best = None
            for a in agents:
                if a.get("a_listener") == listener and time.time() - a.get("a_last_tick", 0) < 30:
                    if best is None or a.get("a_last_tick", 0) > best.get("a_last_tick", 0):
                        best = a
            if best:
                return best["a_id"]
        time.sleep(1)
    raise TimeoutError("no agent on %s registered" % listener)

def send_raw(aid, cmdline):
    tok = _token()
    r = _auth_http("POST", "/agent/command/raw", {"id": aid, "cmdline": cmdline})
    if not r.get("ok"):
        raise RuntimeError("send_raw failed: %r" % r)

def send_execute(aid, cmdline, args):
    # data 里**必须**自带 command（服务器 CreateCommand 只从 data 取，缺了直接回
    # "'command' 参数未设置"，pl_main.go:1136）；cmdline 只是 UI/日志的显示串，
    # 不参与分派。已有调用方（gap10/p03/p02）在 args 里显式带了 command/subcommand
    # —— setdefault 保证不覆盖它们，只补漏。
    _tok = str(cmdline).split()
    d = dict(args)
    d.setdefault("command", _tok[0] if _tok else "")
    if len(_tok) > 1:
        d.setdefault("subcommand", _tok[1])
    tok = _token()
    r = _auth_http("POST", "/agent/command/execute",
              {"id": aid, "ui": False, "cmdline": cmdline,
               "data": json.dumps(d), "ax_hook_id": "", "ax_handler_id": "",
               "wait_answer": True})
    if not r.get("ok"):
        raise RuntimeError("send_execute failed: %r" % r)

def console_poll(aid, substr, timeout=60):
    """轮询 Consoles 表，返回包含 substr 的最新消息 JSON 字符串；超时抛异常。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            con = sqlite3.connect("file:%s?mode=ro" % DB, uri=True, timeout=5)
            rows = con.execute(
                "SELECT CAST(Packet AS TEXT) FROM Consoles WHERE AgentId=? ORDER BY Id DESC LIMIT 40",
                (aid,)).fetchall()
            con.close()
        except Exception:
            rows = []
        for (data,) in rows:
            if substr in data:
                return data
        time.sleep(1)
    raise TimeoutError("console: %r not seen for %s in %ss" % (substr, aid, timeout))

def roundtrip(listener, cmd="getuid", timeout=90, keep=False):
    """单传输任务往返：生成→启动→注册→命令→断言 type=107 完成。"""
    print("[%s] roundtrip ..." % listener, flush=True)
    try:
        bin_path = gen_agent(listener)
        pid = launch(bin_path)
        aid = wait_registered(listener, pid)
        send_raw(aid, cmd)
        msg = console_poll(aid, '"a_completed":true', timeout)
        text = json.loads(msg).get("a_text", "")
        print("[%s] PASS  %s -> %r" % (listener, cmd, text[:80]), flush=True)
        return True
    except Exception as e:
        print("[%s] FAIL  %s" % (listener, e), flush=True)
        return False
    finally:
        if not keep:
            kill(pid) if 'pid' in dir() else None

def cmd_all(listeners, timeout):
    results = [(L, roundtrip(L, timeout=timeout)) for L in listeners]
    ok = sum(1 for _, p in results if p)
    print("SUMMARY: %d/%d PASS" % (ok, len(results)), flush=True)
    sys.exit(0 if ok == len(results) else 1)

def cmd_matrix(listener, timeout):
    """Phase 3 gate: check BOF → resolver → autopush → 执行 → 断言标记。"""
    print("[%s] matrix chain ..." % listener, flush=True)
    bin_path = gen_agent(listener, "mx")
    pid = launch(bin_path)
    try:
        aid = wait_registered(listener, pid)
        send_raw(aid, "execute bof /root/cc/run/extenders/linux_agent/bofs/kernel_exploit_check.o")
        console_poll(aid, "[matrix] MATCH", timeout)
        console_poll(aid, "[matrix] execute queued", timeout)
        # memfd child stdout does not flow through the BOF channel; assert the
        # BOF's own exit report + the payload's on-disk marker via a follow-up cat.
        console_poll(aid, "child exited, status=0x0", timeout)
        send_raw(aid, "cat /tmp/axm_marker")
        m4 = console_poll(aid, "AXM_CHAIN_OK", timeout)
        print("[%s] PASS  %s" % (listener, json.loads(m4).get("a_text", "")[:80]), flush=True)
        return True
    except Exception as e:
        print("[%s] FAIL  %s" % (listener, e), flush=True)
        return False
    finally:
        kill(pid)

def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    cmd = args[0]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args else default
    if cmd == "roundtrip":
        ok = roundtrip(opt("--listener", "TestHTTP"), opt("--cmd", "getuid"),
                       int(opt("--timeout", 90)), "--keep" in args)
        sys.exit(0 if ok else 1)
    elif cmd == "all":
        ls = opt("--listeners", "TestHTTP,TestWS,TestHTTPS,TestWSS,gopher").split(",")
        cmd_all(ls, int(opt("--timeout", 90)))
    elif cmd == "matrix":
        ok = cmd_matrix(opt("--listener", "TestHTTP"), int(opt("--timeout", 120)))
        sys.exit(0 if ok else 1)
    else:
        print("unknown command", cmd)
        sys.exit(2)

if __name__ == "__main__":
    main()
