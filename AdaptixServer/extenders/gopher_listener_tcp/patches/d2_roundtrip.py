#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D2（两次一密）修复的功能判据 —— 真隧道往返。

为什么必须单独写：
  `l2p_e2e.py` 只验「agent 注册得上」。而 D2 的修复**只作用在 tunnel/terminal 的
  加密路径上**（listener `pl_transport.go` ↔ agent `tasks.go` 的 jobTunnel/jobTerminal）。
  两端方向标签若不一致，注册照常全绿而隧道乱码 ⇒ **e2e 对 D2 没有判别力**。

判据 = 把已知明文送进隧道、经变换后**逐字节**收回来。这条路径一次走遍四个方向标签：
    server→agent : listener enc = TunS2A ，agent dec = TunS2A
    agent→server : agent enc = TunA2S ，listener dec = TunA2S
任一处标签写错 / KDF 体两端不同 / 只改了一侧 ⇒ 回来的必是乱码，或干脆卡死到超时。

★ 假绿防线（本判据自己得能红，否则它不是门）：
  A. 起隧道**之前**连 l_port 必须**失败** —— 否则后面的 echo 可能撞上了别的既有服务
  B. echo 返回的是**变换过**的明文（前缀 + 反转），所以「收到正确字节」证明载荷
     真的穿过了两端加解密，而不只是 TCP 握手成功
  C. l_port / t_port 都取内核分配的空闲口，避开 rig 与运行态既有监听
  D. ★ 起隧道前先 grep **即将运行的那个 agent 二进制**里有没有四个标签 ——
     「源码改了、被测二进制是旧的」是这一轮实际踩到的坑（rig 的 src_gopher 曾是旧版，
     e2e 照样全绿）。这条不成立就直接判 FAIL，不进入往返。

用法: d2_roundtrip.py <listener> [--port 39012] [--timeout 240]
"""
import os
import re
import socket
import subprocess
import sys
sys.dont_write_bytecode = True   # ★ 批次13：本文件 import l2p_e2e，不许在交付树里写 __pycache__
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import l2p_e2e as L                                     # noqa: E402  复用 call/login/agents/generate/BUILD

OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"
LABELS = ["axc2/tunnel/s2a/v1", "axc2/tunnel/a2s/v1",
          "axc2/terminal/s2a/v1", "axc2/terminal/a2s/v1"]
MARKER = b"AXD2-PROBE-0123456789abcdef"
EXPECT = b"AXD2:" + MARKER[::-1]        # echo 服务的变换：前缀 + 反转
WORK = "/root/gp5probe/tmp/e2e"


def free_port():
    """向内核要一个空闲口。★ 关掉就让出，落在这个口上的概率极低；
    且判据 A 会独立验证起隧道前它确实没人听。"""
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Echo(object):
    """照着 t_host:t_port 收字节，回「前缀+反转」。★ 变换是刻意的：
    只有载荷真的穿过隧道，收到的才会是变换后的结果。"""

    def __init__(self):
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(8)
        self.port = self.srv.getsockname()[1]
        self.n_conn = 0
        self.stop = False
        self.t = threading.Thread(target=self._loop)
        self.t.daemon = True

    def start(self):
        self.t.start()

    def _loop(self):
        self.srv.settimeout(1.0)
        while not self.stop:
            try:
                c, _ = self.srv.accept()
            except socket.timeout:
                continue
            except Exception:
                return
            self.n_conn += 1
            threading.Thread(target=self._serve, args=(c,)).start()

    @staticmethod
    def _serve(c):
        try:
            c.settimeout(15)
            while True:
                d = c.recv(4096)
                if not d:
                    break
                c.sendall(b"AXD2:" + d[::-1])
        except Exception:
            pass
        finally:
            try:
                c.close()
            except Exception:
                pass

    def close(self):
        self.stop = True
        try:
            self.srv.close()
        except Exception:
            pass


def try_connect(port, timeout=4.0):
    """返回 (连通?, 说明)。用于判据 A（必须失败）与判据 B（必须成功）。"""
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect(("127.0.0.1", port))
        return True, "connected"
    except Exception as e:
        return False, "%s: %s" % (type(e).__name__, e)
    finally:
        try:
            s.close()
        except Exception:
            pass


def observe(lport, agent_pid, echo):
    """红了才取证。★ 判据只回答「通/不通」，**不通的原因**得靠中间观测：
       - 谁在听 l_port？（凡同机部署，ss 的 pid 直接指出是 agent 侧还是 teamserver 侧）
       - agent 到底有没有去连 echo 服务？（echo.n_conn）
       - 这条隧道在 teamserver 眼里是什么状态？（/tunnel/list）
    少了这三样，"隧道不通"就只是一句症状，分不清是我的判据语义用错还是 D2 真的不一致。"""
    out = ["     ── 中间观测 ──"]
    try:
        s = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True, timeout=10).stdout
        hit = [l for l in s.splitlines() if (":%d " % lport) in l]
        out.append("     ss ': %d'  -> %s" % (lport, hit if hit else "没有任何进程在听这个口"))
        apid = [l for l in s.splitlines() if ("pid=%d," % agent_pid) in l]
        out.append("     agent(pid=%d) 在听的: %s" % (agent_pid, [x.split()[3] for x in apid] or "无"))
    except Exception as e:
        out.append("     ss 失败: %s" % e)
    out.append("     echo 服务被连过 %d 次（>0 说明 agent 确实拨到了目标）" % echo.n_conn)
    return "\n".join(out)


def start_agent(tok, listener, timeout):
    """generate → 落盘 → 起进程 → 等 a_pid == 本进程 pid 的注册条目。
    ★ 判据用 a_pid 精确匹配：rig 的 baseline 里本来就有十几条历史注册，只按
      a_listener 找一定会撞上旧条目（那正是「代理量当判据」那一类假绿）。"""
    os.makedirs(WORK, exist_ok=True)
    out = os.path.join(WORK, "d2_agent_%s.bin" % listener)
    size, dt = L.generate(tok, listener, out)
    if size is None:
        return None, None, out, "generate 失败"
    print("%s generate 成功: %d B  耗时 %.1fs" % (INFO, size, dt))

    log = out + ".log"
    lf = open(log, "wb")
    p = subprocess.Popen([out], stdin=subprocess.DEVNULL, stdout=lf,
                         stderr=subprocess.STDOUT, start_new_session=True, cwd=WORK)
    lf.close()
    print("%s agent pid=%d  log=%s" % (INFO, p.pid, log))

    t0 = time.time()
    while time.time() - t0 < timeout:
        if p.poll() is not None:
            return p, None, out, "agent 提前退出 rc=%s（%.0fs）" % (p.returncode, time.time() - t0)
        for a in (L.agents(tok) or []):
            if a.get("a_listener") == listener and a.get("a_pid") == str(p.pid):
                return p, a, out, None
        time.sleep(3)
    return p, None, out, "%.0fs 内未出现 a_pid=%d 的注册条目" % (timeout, p.pid)


def main():
    argv = sys.argv[1:]
    listener, port, timeout = None, 39012, 240
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--port":
            port = int(argv[i + 1]); i += 2; continue
        if a == "--timeout":
            timeout = int(argv[i + 1]); i += 2; continue
        if a.startswith("--"):
            print("%s 未知参数 %s" % (BAD, a)); return 2
        listener = a; i += 1
    if not listener:
        print("用法: d2_roundtrip.py <listener> [--port 39012] [--timeout 240]")
        return 2

    L.BASE = "https://127.0.0.1:%d/api" % port
    print("=== D2 隧道往返判据  listener=%s  api=%s ===" % (listener, L.BASE))

    tok = L.login()
    if not tok:
        print("%s login 失败" % BAD); return 1

    p, ag, out, err = start_agent(tok, listener, 150)
    res = []          # (名字, 通过?)
    try:
        if err:
            print("%s %s" % (BAD, err))
            if p is not None and p.poll() is None:
                try: os.kill(p.pid, 9)
                except Exception: pass
            print("RESULT: 0/5 PASS")
            return 1
        aid = ag.get("a_id")
        print("%s 注册成功 a_id=%s a_listener=%s a_pid=%s" % (OK, aid, ag.get("a_listener"), ag.get("a_pid")))

        # ── D：被测二进制里必须真的带 D2 代码 ────────────────────────────
        raw = open(out, "rb").read()
        miss = [s for s in LABELS if s.encode() not in raw]
        tag = "%s %d B" % (os.path.basename(out), len(raw))
        res.append(("D 被测 agent 二进制含四个方向标签", not miss))
        if miss:
            print("%s D 缺标签: %s（其余判据无意义，直接停）" % (BAD, miss))
            print("RESULT: %d/5 PASS" % sum(1 for _, k in res if k))
            return 1
        print("%s D %s 含四个方向标签 [ok]" % (OK, tag))

        echo = Echo(); echo.start()
        lport = free_port()
        print("%s echo 服务 127.0.0.1:%d（回 %s+反转）  l_port=%d" % (INFO, echo.port, b"AXD2:", lport))

        # ── A：起隧道前 l_port 必须没人听 ────────────────────────────────
        conn, why = try_connect(lport)
        res.append(("A 起隧道前 l_port 未被监听（阴性对照）", not conn))
        if conn:
            print("%s A l_port=%d 起隧道前就连得上 ⇒ 判据无效，停" % (BAD, lport))
            echo.close(); print("RESULT: %d/5 PASS" % sum(1 for _, k in res if k)); return 1
        print("%s A 起隧道前 %d 连不上（%s）" % (OK, lport, why))

        # ── 起隧道 ★ 必须用 lportfwd，不能用 rportfwd ────────────────────
        # 实测（2026-09-16）：`/tunnel/start/rportfwd` 在本交付物里**是死的** ——
        # REVERSE 型由 `ts_tunnels.go:317` 走 `Callbacks.Reverse`，而 gopher 插件的
        #   func TunnelMessageReverse(tunnelId, port int) adaptix.TaskData {
        #       var packData []byte
        #       /// START CODE HERE
        #       /// END CODE HERE     ← 模板占位符，从未实现
        #       return makeProxyTask(packData) }
        # 发出去的是**空任务**，agent 永远收不到"去 listen l_port"的指令。
        # 现象：teamserver 报隧道 ok/active，但 l_port 无人监听、目标侧零连接。
        # LOCAL_PORT 型走 `TunnelMessageConnectTCP`（真实现）：teamserver 自己
        # net.Listen(l_port)，每来一个连接就令 agent 拨 t_host:t_port。
        # ★ 两者的加解密路径**完全相同**（都是 TUNNEL_PACK + jobTunnel 的 D2 派生），
        #   所以换型不影响本判据对 D2 的判别力。
        st, body = L.call("/tunnel/start/lportfwd",
                          {"agent_id": aid, "listen": True, "desc": "d2-probe",
                           "l_host": "127.0.0.1", "l_port": lport,
                           "t_host": "127.0.0.1", "t_port": echo.port},
                          tok, timeout=120)
        print("%s /tunnel/start/lportfwd -> %s %s" % (INFO, st, body[:300]))
        tid = None
        try:
            j = __import__("json").loads(body)
            if j.get("ok"):
                tid = j.get("message")
        except Exception:
            pass
        res.append(("E /tunnel/start/lportfwd 返回 ok", bool(tid)))
        if not tid:
            print("%s E 隧道没起来 ⇒ 后面的往返无从谈起" % BAD)
            echo.close(); print("RESULT: %d/5 PASS" % sum(1 for _, k in res if k)); return 1
        print("%s E 隧道已建 p_tunnel_id=%s" % (OK, tid))

        # ── B+C：真往返 ──────────────────────────────────────────────────
        got, why = None, ""
        for _ in range(20):                       # 隧道建链可能慢，重试而非睡死
            s = socket.socket(); s.settimeout(6)
            try:
                s.connect(("127.0.0.1", lport))
                s.sendall(MARKER)
                got = s.recv(4096)
                break
            except Exception as e:
                why = "%s: %s" % (type(e).__name__, e)
                time.sleep(1.5)
            finally:
                try: s.close()
                except Exception: pass

        if got is None:
            print(observe(lport, p.pid, echo))
            try:
                st, tl = L.call("/tunnel/list", None, tok, "GET", timeout=30)
                print("     /tunnel/list -> %s" % (tl or "")[:400])
            except Exception as e:
                print("     /tunnel/list 失败: %s" % e)

        res.append(("B l_port 已可连（阳性对照）", got is not None))
        print("%s B l_port=%d %s" % (OK if got is not None else BAD, lport,
                                     "已连通" if got is not None else ("连不上: " + why)))
        exact = (got == EXPECT)
        res.append(("C 明文经隧道往返后逐字节一致", exact))
        print("%s C 发 %d B → 收 %s" % (OK if exact else BAD, len(MARKER),
                                        repr(got)[:120] if got is not None else "None"))
        if not exact:
            print("     期望 %s" % repr(EXPECT)[:120])
            print("     ★ 不等即说明两端 D2 派生不一致（标签错/KDF 体不同/只改了一侧）")

        # ── 清理 ─────────────────────────────────────────────────────────
        st, body = L.call("/tunnel/stop", {"p_tunnel_id": tid}, tok, timeout=60)
        print("%s /tunnel/stop -> %s %s" % (INFO, st, body[:120]))
        echo.close()
    finally:
        if p is not None and p.poll() is None:
            try: os.kill(p.pid, 9)
            except Exception: pass

    bad = [n for n, k in res if not k]
    for n, k in res:
        print("   %s %s" % ("PASS" if k else "FAIL", n))
    print("RESULT: %d/%d PASS" % (len(res) - len(bad), len(res)))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
