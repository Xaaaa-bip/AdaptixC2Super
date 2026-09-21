#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次9 ★端到端验收 —— rportfwd（反向端口转发）链路**真的通**吗？

    python3 l2p_rportfwd_e2e.py [--listener tcp1] [--keep]

★★ 为什么静态判据不够、必须做网络级 e2e

  `l2p_rportfwd.py` 的 V1–V8 全是**静态**判据（锚点在场、结构同构、gofmt、回滚点可重放）。
  它们能证「代码写成了对的形」，**证不了「链路通」**。本批次要修的恰好是
  「**静默假成功**」：TunnelMessageReverse 是空壳（下发空任务）、Type-1 缺 case
  （监听结果回不来）、Type-2 缺 case（新通道通告没人收）、agent 侧没监听也没预置连接表。
  这四处**任何一处写错，V1–V8 可以全绿而链路全断** —— 因为它们量的是「代码长什么样」，
  不是「字节有没有过」。所以必须真起监听、真拨号、真过流量。

★★ 判据设计（每条都要能红，且红必须红在真理由上）

  J1 隧道登记       /tunnel/list 里出现 p_agent_id/p_port/p_fhost/p_fport 全对上的条目。
                    ★ 本判据**只看登记**，不看 `p_active` —— 见 J1b 的注释：
                      REVERSE 的 p_active 恒为 false（核心侧既存缺陷），拿它当判据是假红。
  J1b Type-1 真跑了  /agent/task/list 里出现任务消息
                    `Reverse port forward '<lport>' to '<t_host>:<t_port>'`。
                    ★ 这条消息只由 `TsTunnelUpdateRportfwd` 的成功分支产生，而它只在
                      Type-1 的 `case COMMAND_TUNNEL_REVERSE` 里被调用 ⇒ 缺 case 则
                      `default: continue` ⇒ 消息根本不存在。**非重言式**。
  J2 监听归属       `ss -ltnp` 里 :LPORT 的占用者 **必须是 agent 的 pid**，不是 teamserver 的。
                    ★ 这是「监听发生在**目标机**上」的唯一硬证据；只看端口开没开是不够的
                    （teamserver 自己也可能开同一个端口 —— 那就完全不是 rportfwd 了）。
  J3 数据面         连 LPORT → 收到**靶机 A 的 token**，逐字节相等。
  J4 目标真被使用   第二条隧道指向**靶机 B**（不同 token）⇒ 必须收到 B 的 token。
                    ★ 没有这条，一个「永远拨我写死的那个地址」的实现也能让 J3 全绿。
  J5 负向对照       停掉靶机 A 后，**同一个隧道端口**再连 ⇒ 拿不到 A 的 token。
                    ★ 没有这条，一个「本地回声」实现也能让 J3 全绿（J3 只证"有字节回来"）。
                    ★ 顺序很重要：J5 必须在 J3/J4 之后（它要破坏 A 的靶机）。
                    ★★ 它**必须挂在前置正判据上**：A 的隧道没建成时 probe 返回 b""
                       ≠ token ⇒ J5 会**无条件报绿**（「空值即通过」，负向观测家族的变种）。
                       故：先要求 `data_ok["A"] and "lport" in tgt["A"]`，否则直接判 FAIL
                       并写明「无法评估」——没有判别力的绿比红更危险。
  J6 收尾干净       隧道停止后 LPORT 不再被 agent 监听。

★ 环境：**全程只在探针 rig（39012）上做**，绝不碰交付运行态（38212）。
  rig 的插件刷新逐条镜像 l2p_deploy.sh 的 do_install（同样两个 .so + 四件套 + src_gopher），
  只是把 RLST/RAGT 指向 rig ⇒ 交付运行树零接触。
★ scratch 一律 /root/gp5probe/**（/tmp 会触发 p17 T6 的时间窗假红）。
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
sys.dont_write_bytecode = True   # ★ 批次13：import l2p_e2e 前先关掉字节码缓存
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import l2p_e2e as E                                    # noqa: E402  复用登录/生成/列表

OK, BAD, INFO = E.OK, E.BAD, E.INFO
RIG = "/root/gp5probe/l2prun"
LST = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp"
AGT = "/root/cc/AdaptixServer/extenders/gopher_agent"
FOUR = ("ax_config.axs", "config.yaml")

LPORT_A = 45111        # agent 侧监听（rportfwd 的「对外端口」）
LPORT_B = 45112
LPORT_C = 45113        # J1b 负向判据专用：由本脚本自己占住，逼 agent 监听失败
CHAL = "AXC2-RPF-CHAL"


# ── 判据小工具 ────────────────────────────────────────────────────────────────
# ★ FAILS 记录每一条红，用于「真红 / 已登记开放项」分类（见 KNOWN_OPEN 与 main）。
#   标签取失败消息的首个空白分隔词（判据消息都以判据名开头，如 "J6 ..."）；
#   **认不出的标签一律算真红** —— 分类失败时的默认方向必须是保守的那一侧。
FAILS = []


def ck(cond, okmsg, badmsg):
    print(("  %s %s" % (OK, okmsg)) if cond else ("  %s %s" % (BAD, badmsg)))
    if not cond:
        FAILS.append(((str(badmsg).split() or [""])[0], str(badmsg)))
    return bool(cond)


def listen_owner(port):
    """返回 `ss -ltnp` 里监听 :port 的 pid（取不到 ⇒ None）。**不猜**。"""
    try:
        out = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True,
                             timeout=10).stdout
    except Exception:
        return None
    for line in out.splitlines():
        if ":%d " % port in line:
            m = re.search(r"pid=(\d+)", line)
            if m:
                return int(m.group(1))
    return None


def port_free(port):
    try:
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", port))
        s.close()
        return True
    except OSError:
        return False


def probe(port, timeout=6.0):
    """连 port，发口令，读回靶机的 token（读不到就返回 b""）。"""
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect(("127.0.0.1", port))
        s.sendall(CHAL.encode())
        buf = b""
        while b"\n" not in buf and len(buf) < 256:
            d = s.recv(256)
            if not d:
                break
            buf += d
        return buf.strip()
    except Exception:
        return b""
    finally:
        try:
            s.close()
        except Exception:
            pass


# ── 靶机模式（本脚本被自己 spawn）─────────────────────────────────────────────
def target_mode(port, token):
    """极简 TCP 靶机：收到任何东西就回 `<token>\\n`。

    ★ 用独立模式而不是 `python3 -c "..."`：省掉一整层 shell 引号地狱
      （本项目在补丁脚本上已经因为引号截断毁过一次文件）。
    ★ port=0 ⇒ 由内核挑空闲端口并把真实端口打到 stdout，交父进程读 ⇒ **无竞态**。
    """
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(16)
    print("PORT=%d" % s.getsockname()[1], flush=True)
    while True:
        try:
            c, _ = s.accept()
        except OSError:
            return 0
        try:
            c.settimeout(5)
            try:
                c.recv(1024)
            except Exception:
                pass
            c.sendall((token + "\n").encode())
        except Exception:
            pass
        finally:
            try:
                c.close()
            except Exception:
                pass


def spawn_target(token):
    """起一个靶机，返回 (Popen, port)。端口由内核分配后回报，不用猜。"""
    p = subprocess.Popen([sys.executable, os.path.abspath(__file__),
                          "--target", "--port", "0", "--token", token],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, start_new_session=True)
    t0 = time.time()
    while time.time() - t0 < 15:
        line = p.stdout.readline()
        if not line:
            break
        m = re.match(r"PORT=(\d+)", line.strip())
        if m:
            return p, int(m.group(1))
    try:
        p.kill()
    except Exception:
        pass
    return None, None


# ── rig 刷新（逐条镜像 do_install，只是把 RLST/RAGT 指向 rig）─────────────────
def refresh_rig():
    print("== 刷新 rig 的插件（镜像 do_install；交付运行树零接触）==")
    rlst = os.path.join(RIG, "extenders/gopher_listener_tcp")
    ragt = os.path.join(RIG, "extenders/gopher_agent")
    steps = [("listener .so", os.path.join(LST, "dist/listener_gopher_tcp.so"),
              os.path.join(rlst, "listener_gopher_tcp.so")),
             ("agent .so", os.path.join(AGT, "dist/agent_gopher.so"),
              os.path.join(ragt, "agent_gopher.so"))]
    for f in FOUR:
        steps.append(("listener " + f, os.path.join(LST, "dist", f), os.path.join(rlst, f)))
        steps.append(("agent " + f, os.path.join(AGT, "dist", f), os.path.join(ragt, f)))
    for name, src, dst in steps:
        if not os.path.exists(src):
            print("  %s 缺 %s（先跑 l2p_deploy.sh build）" % (BAD, src))
            return False
        r = subprocess.run(["cp", "-f", src, dst], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(dst):
            print("  %s 拷 %s 失败：%s" % (BAD, name, (r.stderr or "").strip()[:200]))
            return False
        print("  %s %s" % (INFO, name))
    # src_gopher 必须**先删再拷**（do_install 也是 rm -rf 后 cp -r）——
    # 直接 cp -r 到已存在的目录会变成 src_gopher/src_gopher。
    # ★ 但探针 rig 比交付树多留一手：旧的那份**改名留档一代**而不是 rm -rf
    #   （rig 里有 .bak-chacha 之类回滚点，删掉会让 l2p_crypto.py revert 失去后路）。
    #   交付树用的是 do_install 那条路径，**本脚本不碰它**。
    dsrc = os.path.join(ragt, "src_gopher")
    prev = os.path.join(ragt, "src_gopher.prev-rpf")
    for cmd in (["rm", "-rf", prev],
                ["bash", "-c", "[ -d %s ] && mv %s %s || true" % (dsrc, dsrc, prev)],
                ["cp", "-r", os.path.join(AGT, "dist/src_gopher"), dsrc]):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print("  %s %s 失败：%s" % (BAD, cmd[0], (r.stderr or "").strip()[:200]))
            return False
    print("  %s src_gopher（旧的一代 → src_gopher.prev-rpf，再 cp -r 新的）" % INFO)
    for name, src, dst in (("agent .so", os.path.join(AGT, "dist/agent_gopher.so"),
                            os.path.join(ragt, "agent_gopher.so")),
                           ("src_gopher/utils/crypt.go",
                            os.path.join(AGT, "dist/src_gopher/utils/crypt.go"),
                            os.path.join(dsrc, "utils/crypt.go"))):
        a = subprocess.run(["md5sum", src], capture_output=True, text=True).stdout.split()[0]
        b = subprocess.run(["md5sum", dst], capture_output=True, text=True).stdout.split()[0]
        if not ck(a == b, "%s 拷贝后 md5 一致（%s）" % (name, a[:12]),
                  "%s 拷贝后 md5 不一致 %s vs %s" % (name, a[:12], b[:12])):
            return False
    return True


def rig_cycle():
    """重启 rig。

    ★★ 踩坑记录（本批次）：`rig_run.sh cycle` 用 subprocess 的**管道**收输出时
       **永不返回** —— `do_start` 用 `( cd $RIG && ... setsid nohup ./adaptixserver ... & )`
       起实例，被 setsid 脱离会话的 teamserver 仍持有那条管道的写端 ⇒ bash 退出后
       管道不 EOF ⇒ `communicate()` 一直等。（与批次8 的「setsid 持有 SSH 通道 ⇒
       ssh_run 不返回」是**同一族**，判据也一样：**不要因命令没返回就判定失败**。）
       ⇒ 两条应对都用了：
         ① 输出重定向到**文件**而不是管道，stdin=DEVNULL（把通道与后台进程解耦）；
         ② **不以命令返回码为判据**，改以**实测状态**为判据 —— 39012 有人在听
            且占用者 cwd 就是 rig 目录（rig_run.sh 的 rpid() 用的同款判据）。
    """
    log = "/root/gp5probe/tmp/rig_cycle.log"
    os.makedirs(os.path.dirname(log), exist_ok=True)
    print("== 重启 rig ==（输出走文件不走管道）")
    try:
        with open(log, "wb") as lf:
            subprocess.run(["bash", "/root/gp5probe/rig_run.sh", "cycle"],
                           stdin=subprocess.DEVNULL, stdout=lf,
                           stderr=subprocess.STDOUT, timeout=180)
    except subprocess.TimeoutExpired:
        print("  %s cycle 未在 180s 内返回（已知族）⇒ 不看返回码，改看实测状态" % INFO)
    t0 = time.time()
    while time.time() - t0 < 40:
        owner = listen_owner(39012)
        if owner:
            try:
                cwd = os.path.realpath("/proc/%d/cwd" % owner)
            except Exception:
                cwd = ""
            if cwd == RIG:
                print("  %s rig LISTEN 39012，占用者 pid=%d 且 cwd=%s（权威判据）"
                      % (OK, owner, cwd))
                return True
        time.sleep(2)
    print("  %s rig 未在 40s 内起来（39012 占用者=%s）" % (BAD, listen_owner(39012)))
    try:
        print(open(log, errors="replace").read()[-1500:])
    except Exception:
        pass
    return False


# ── 主流程 ────────────────────────────────────────────────────────────────────
def run(listener, keep):
    print("=== 批次9 rportfwd 端到端验收（rig 39012）===")
    for p in (LPORT_A, LPORT_B):
        if not port_free(p):
            print("%s 起始前提不成立：%d 已被占用" % (BAD, p))
            return False

    if not refresh_rig():
        return False
    if not rig_cycle():
        print("%s rig 未能重启" % BAD)
        return False

    tok = E.login()
    if not tok:
        print("%s 登录 rig 失败" % BAD)
        return False
    print("%s 已登录 rig" % OK)

    if listener is None:
        st, body = E.call("/listener/list", None, tok, "GET", timeout=60)
        names = [l.get("l_name") for l in json.loads(body)
                 if l.get("l_protocol") == "tcp"]
        if not names:
            print("%s rig 上没有 tcp listener" % BAD)
            return False
        listener = names[0]
    print("%s 用 listener=%s（l_protocol=tcp）" % (INFO, listener))
    proto, perr = E.listener_protocol(tok, listener)
    if perr:
        print("%s 取不到 %s 的传输类型：%s" % (BAD, listener, perr))
        return False
    print("%s 权威字段 l_protocol=%s" % (INFO, proto))

    # ── 靶机（两个，token 不同）──────────────────────────────────────────────
    tag = "%x" % int(time.time())
    tgt = {}
    for name in ("A", "B"):
        token = "RPF-%s-%s" % (name, tag)
        p, port = spawn_target(token)
        if p is None:
            print("%s 靶机 %s 起不来" % (BAD, name))
            return False
        tgt[name] = {"p": p, "port": port, "token": token.encode()}
        print("%s 靶机 %s: 127.0.0.1:%d token=%s" % (INFO, name, port, token))
    # ★ 靶机自证：先直连一次，证明「不经过任何隧道也能拿到 token」——
    #   没有这一步，J3 绿了也分不清是隧道通了还是靶机本来就通。
    for name, d in tgt.items():
        got = probe(d["port"])
        if not ck(got == d["token"], "靶机 %s 直连可用（token 对得上）" % name,
                  "靶机 %s 直连都拿不到 token（got=%r）⇒ 后面所有 J3/J4 都不可判" % (name, got)):
            return False

    agent_id = None          # Adaptix 会话 id（/api 用）
    agent_os_pid = None      # agent 进程的 OS pid（只有 ss 归属核查用）
    tunnels = []
    ok = True
    try:
        # ── 起 agent ─────────────────────────────────────────────────────────
        os.makedirs(E.WORK, exist_ok=True)
        out = os.path.join(E.WORK, "agent_rpf_%s.bin" % tag)
        size, dt = E.generate(tok, listener, out)
        if size is None:
            print("%s generate 失败" % BAD)
            return False
        print("%s generate 成功 %d B（%.1fs）" % (INFO, size, dt))
        lf = open(os.path.join(E.WORK, "agent_rpf_%s.log" % tag), "wb")
        ap = subprocess.Popen([out], stdin=subprocess.DEVNULL, stdout=lf,
                              stderr=subprocess.STDOUT, start_new_session=True,
                              cwd=E.WORK)
        lf.close()
        t0 = time.time()
        while time.time() - t0 < 90:
            if ap.poll() is not None:
                print("%s agent 提前退出 rc=%s" % (BAD, ap.returncode))
                return False
            for a in (E.agents(tok) or []):
                if a.get("a_pid") == str(ap.pid):
                    agent_id = a.get("a_id") or ""
                    agent_os_pid = str(a.get("a_pid") or "")
                    break
            if agent_id:
                break
            time.sleep(3)
        if not agent_id:
            print("%s 90s 内没等到 agent 注册（os pid=%d）" % (BAD, ap.pid))
            return False
        # ★ 两个 id 必须分清（本批次踩到）：
        #   `a_id`      = Adaptix 的**会话 id**（如 97a9a505）—— 所有 /api 要的是它；
        #   `a_pid`     = agent 进程的 **OS pid**     —— 只有 `ss` 归属核查要它。
        #   拿 a_pid 去调 /tunnel/start/rportfwd ⇒ `agent '<pid>' does not exist`。
        print("%s agent 已注册 a_id=%s os_pid=%s（%.0fs）"
              % (OK, agent_id, agent_os_pid, time.time() - t0))

        # ── J1 + J2 + J3：隧道 A ─────────────────────────────────────────────
        for name, lport in (("A", LPORT_A), ("B", LPORT_B)):
            d = tgt[name]
            st, body = E.call("/tunnel/start/rportfwd",
                              {"agent_id": agent_id,
                               "listen": True,
                               "desc": "rpf-e2e-%s" % name,
                               "port": lport,
                               "t_host": "127.0.0.1",
                               "t_port": d["port"]}, tok, timeout=120)
            print("%s tunnel/start/rportfwd(%s) -> %s %s"
                  % (INFO, name, st, body[:200]))
            if st != 200 or '"ok":false' in body.replace(" ", ""):
                ok &= ck(False, "", "隧道 %s 创建请求被拒（%s）" % (name, body[:200]))
                continue
            try:
                d["tid"] = json.loads(body).get("message", "")
            except Exception:
                d["tid"] = ""
            d["lport"] = lport
            tunnels.append(d)

            # J2：等 agent 真的在那个端口上 LISTEN，且**占用者必须是 agent**
            t0 = time.time()
            owner = None
            while time.time() - t0 < 40:
                owner = listen_owner(lport)
                if owner:
                    break
                time.sleep(2)
            ok &= ck(owner is not None and str(owner) == agent_os_pid,
                     "J2 隧道 %s：:%d 的监听者 pid=%s = agent 的 pid（监听真在目标机上）"
                     % (name, lport, owner),
                     "J2 隧道 %s：:%d 的监听者 pid=%s，而 agent pid=%s ⇒ 不是 agent 在听"
                     % (name, lport, owner, agent_os_pid))

        # J1：/tunnel/list 里**登记齐全**（只看登记，不看 p_active —— 见下方注释）
        st, body = E.call("/tunnel/list", None, tok, "GET", timeout=60)
        try:
            tl = json.loads(body)
        except Exception:
            tl = []
        for d in tunnels:
            hit = [x for x in tl
                   if x.get("p_agent_id") == agent_id
                   and str(x.get("p_port")) == str(d["lport"])
                   and str(x.get("p_fport")) == str(d["port"])]
            ok &= ck(bool(hit), "J1 隧道 :%d → 127.0.0.1:%d 已登记 p_type=%s p_active=%s"
                     % (d["lport"], d["port"], hit[0].get("p_type"), hit[0].get("p_active"))
                     if hit else "",
                     "J1 隧道 :%d → 127.0.0.1:%d 没登记（tl=%s）"
                     % (d["lport"], d["port"], json.dumps(tl)[:300]))
            if hit:
                # ★★ 这里**绝不能**拿 p_active 当判据（本注释订正过本文件里的一个**错说法**）：
                #   本文件原先写「REVERSE 的 p_active 恒为 false，是核心侧既存缺陷」——**错**。
                #   代数：`Active` 建隧道时是 false（ts_tunnels.go:449），而
                #   `tunnel.Active = true` 位于 `TsTunnelStart` 的 if/else **之外**（:346）
                #   ⇒ REVERSE 同样被置位，且时机是**仅下发任务之后、agent 回答之前**
                #   ⇒ 它是**乐观置位**：对「agent 到底有没有把监听起起来」零判别力。
                #   ★ 两种错法都在这一个字段上：当判据 ⇒ 假绿（永远 true）；
                #     说它恒 false ⇒ 假红。**错的不是代码，是判据。**
                #   ⇒ 降为**观察项**，如实记录（本批次不改 core）。
                print("  %s 观察项（不计入 PASS/FAIL）：隧道 :%d p_active=%r p_type=%s"
                      % (INFO, d["lport"], hit[0].get("p_active"), hit[0].get("p_type")))

        # ── J1b（**重设计**）：负向可观测判据 —— 监听起不来时必须把隧道摘掉 ─────────
        # ★ 旧版 J1b 为什么作废（留档：本项目第 11 种假形态 ——「**不可达判据**」）：
        #   旧版查「/agent/task/list 里出现 Reverse port forward '<lport>' to '<t>:<p>'」。
        #   那条消息由 Type-1 的 `case COMMAND_TUNNEL_REVERSE` 写进 task.Message，再经
        #   TsTaskUpdate → TaskManager.Update（core/server/mgr_task.go:181）。而 Update 的
        #   **第一件事**就是 `agent.RunningTasks.Get(updateData.TaskId)`，**未命中即静默
        #   return**（:188-191，连一条日志都没有）。隧道任务走的是 extractTunnelTasks
        #   （ts_tasks.go:81-99），它**从不**登记 RunningTasks ⇒ 该消息永远进不了任务列表
        #   ⇒ **无论链路好坏 J1b 恒红**。
        #   ⇒ 它不是假绿，而是**恒假红** —— 更阴险：它会诱使我去「修」本来正确的代码。
        #     （upstream 亦如此，是既存缺陷，不是本批次引入；beacon 侧同理。）
        # ★ 新版仍是**非重言式**，只是把「正着看」换成「负着看」：
        #   ① 本脚本先自己占住 LPORT_C（绑 0.0.0.0）
        #   ② 再请求对 LPORT_C 起 rportfwd ⇒ agent 的 net.Listen("0.0.0.0:P") 必然
        #      EADDRINUSE ⇒ taskTunnelReverse 回 Result:0
        #   ③ Type-1 的 case 调 TsTunnelUpdateRportfwd(tunnelId, false)
        #   ④ core 的 else 分支执行 **DeleteTunnel**（ts_tunnels.go:493）
        #   ⑤ ⇒ 隧道从 /tunnel/list **消失**
        #   ★ 红只有一条路：那个 case 没生效（default: continue 把结果丢掉）⇒ 不删
        #     ⇒ 隧道一直留在列表里。绿也只有一条路：core 真的执行了删除。
        #   ★ 前置正判据（必须有）：先证明本脚本**占得住**那个端口 —— 否则 agent 会监听
        #     成功、隧道本就不该消失，本判据就失去意义（「负向观测必须先证通道有效」）。
        blocker, blocker_ok = None, False
        try:
            blocker = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            blocker.bind(("0.0.0.0", LPORT_C))
            blocker.listen(8)
            blocker_ok = True
        except OSError as ex:
            print("  %s J1b 前置不成立：占不住 :%d（%s）" % (BAD, LPORT_C, ex))
        ok &= ck(blocker_ok,
                 "J1b 前置：本脚本已占住 :%d（0.0.0.0）⇒ agent 那边必然监听失败" % LPORT_C,
                 "J1b 前置：占不住 :%d ⇒ 本判据**无法评估**（不报绿）" % LPORT_C)
        if blocker_ok:
            st, body = E.call("/tunnel/start/rportfwd",
                              {"agent_id": agent_id, "listen": True,
                               "desc": "rpf-e2e-neg", "port": LPORT_C,
                               "t_host": "127.0.0.1", "t_port": tgt["A"]["port"]},
                              tok, timeout=120)
            print("%s tunnel/start/rportfwd(neg :%d) -> %s %s"
                  % (INFO, LPORT_C, st, body[:160]))
            gone, t0, tid_neg = False, time.time(), ""
            try:
                tid_neg = json.loads(body).get("message", "") or ""
            except Exception:
                pass
            while time.time() - t0 < 30:
                st2, b2 = E.call("/tunnel/list", None, tok, "GET", timeout=60)
                try:
                    tl2 = json.loads(b2)
                except Exception:
                    tl2 = None
                if tl2 is not None:
                    still = [x for x in tl2
                             if x.get("p_agent_id") == agent_id
                             and str(x.get("p_port")) == str(LPORT_C)]
                    if not still:
                        gone = True
                        break
                time.sleep(2)
            ok &= ck(gone,
                     "J1b 监听失败被如实上报：:%d 的隧道已从 /tunnel/list 摘除"
                     "（Type-1 case 生效 + core 真的 DeleteTunnel）" % LPORT_C,
                     "J1b :%d 的监听起不来，隧道却**仍留在** /tunnel/list 里 ⇒ Type-1 的"
                     " case COMMAND_TUNNEL_REVERSE 没生效（结果被 default: continue 丢掉）"
                     % LPORT_C)
            if not gone and tid_neg:
                # 判据已经红了，这里只是不留脏现场（best-effort）
                for path in ("/tunnel/stop", "/tunnel/delete"):
                    try:
                        E.call(path, {"p_tunnel_id": tid_neg}, tok, timeout=30)
                    except Exception:
                        pass
        if blocker is not None:
            try:
                blocker.close()
            except Exception:
                pass
        # J3 + J4：数据面
        # ★ data_ok 是 J5 的**前置正判据**：只有 A 的数据面真的通过，
        #   J5 的「拿不到 token」才有判别力（否则隧道根本没建成，J5 也会绿）。
        # ★★ 批次9d：单发探测**抓不住竞态型 b''** —— 实测同一条隧道「首连绿、次连红」
        #    （两个读者抢同一条 io.Pipe，谁先读到谁拿走；被 G2 拿走的字节永久丢失）。
        #    单发判据在这种缺陷下有 ~50% 概率假绿 ⇒ 改为**连探 REPEATS 次、次次必须命中**。
        REPEATS = 5
        data_ok = {"A": False, "B": False}
        for name in ("A", "B"):
            d = tgt[name]
            if "lport" not in d:
                continue
            hits = [probe(d["lport"], timeout=8) for _ in range(REPEATS)]
            good = sum(1 for h in hits if h == d["token"])
            data_ok[name] = (good == REPEATS)
            ok &= ck(good == REPEATS,
                     "J3/J4 隧道 %s：连 :%d %d/%d 次都拿回**靶机 %s 的** token（数据面稳定）"
                     % (name, d["lport"], good, REPEATS, name),
                     "J3/J4 隧道 %s：连 :%d 只有 %d/%d 次拿到 token（首次 %r）⇒ 数据面**不稳定**"
                     % (name, d["lport"], good, REPEATS, hits[0]))

        # ★ J4 的**交叉**判据：A 的端口绝不能返回 B 的 token（否则就是「地址被忽略」）
        if "lport" in tgt["A"] and "lport" in tgt["B"]:
            ga_hits = [probe(LPORT_A, timeout=8) for _ in range(REPEATS)]
            ga_ok = sum(1 for h in ga_hits if h == tgt["A"]["token"])
            ok &= ck(ga_ok == REPEATS,
                     "J4x 两条隧道不串：:%d 连续 %d 次都只返回 A 的 token" % (LPORT_A, REPEATS),
                     "J4x 两条隧道串了/不稳：:%d 只有 %d/%d 次返回 A 的 token（首次 %r）"
                     "⇒ t_host/t_port 没被真正使用"
                     % (LPORT_A, ga_ok, REPEATS, ga_hits[0]))

        # ── J1c：同端口「停止 → 再启用」（core 的一等生命周期）───────────────
        # ★ core 自己把这个流程定义成一等公民，两处硬证据：
        #   · TsTunnelStop：**"Close runtime resources but KEEP tunnel entry for re-enable."**
        #   · TsTunnelCreate:432-443：inactive entry 直接复用，「keep id for enable/stop
        #     lifecycle」，且 TunnelId 仍由 CRC32(client+agent+"rportfwd"+port) 算出
        #     ⇒ **同一个 id**，随后 TsTunnelStart 的 REVERSE 分支会**重新下发**任务。
        # ★ 缺了这条，一个「监听一停就再也起不来」的实现（旧 listener 占着端口 ⇒
        #   net.Listen EADDRINUSE ⇒ Result:0 ⇒ core DeleteTunnel）也能让 J1/J2/J3/J4 全绿，
        #   而操作员在 GUI 里看到的是「重新启用直接失败、隧道消失」—— J6 那条上游缺陷
        #   造成的真实故障正是这个形态。
        # ★ 三条缺一即红，且都在靶机 A **还活着**时做（数据面那条才有判别力）。
        if "lport" in tgt["A"]:
            E.call("/tunnel/stop", {"p_tunnel_id": tgt["A"]["tid"]}, tok, timeout=60)
            time.sleep(2)
            st, body = E.call("/tunnel/start/rportfwd",
                              {"agent_id": agent_id, "listen": True,
                               "desc": "rpf-e2e-A-rearm", "port": LPORT_A,
                               "t_host": "127.0.0.1", "t_port": tgt["A"]["port"]},
                              tok, timeout=120)
            print("%s tunnel/start/rportfwd(A 再启用) -> %s %s" % (INFO, st, body[:200]))
            ok &= ck(st == 200 and '"ok":false' not in body.replace(" ", ""),
                     "J1c 同端口再启用被接受（走的正是 TsTunnelCreate 的 reuse inactive entry）",
                     "J1c 同端口再启用被拒（%s）⇒ 与 TsTunnelCreate 的 reuse 分支矛盾" % body[:200])
            try:      # id 由 CRC32 推出，理应不变；仍如实刷新，保证 J6 收尾用的是当前 id
                ntid = json.loads(body).get("message", "") or ""
                if ntid:
                    tgt["A"]["tid"] = ntid
            except Exception:
                pass
            owner, t0 = None, time.time()
            while time.time() - t0 < 30:
                owner = listen_owner(LPORT_A)
                if owner:
                    break
                time.sleep(2)
            ok &= ck(owner is not None and str(owner) == agent_os_pid,
                     "J1c 再启用后 :%d 仍由 agent 监听（pid=%s）" % (LPORT_A, owner),
                     "J1c 再启用后 :%d 的监听者 pid=%s（agent pid=%s）⇒ agent 没能重新监听"
                     % (LPORT_A, owner, agent_os_pid))
            got = probe(LPORT_A, timeout=8)
            ok &= ck(got == tgt["A"]["token"],
                     "J1c 再启用后数据面仍通（拿回 A 的 token）",
                     "J1c 再启用后数据面不通（got=%r，期望 %r）"
                     % (got, tgt["A"]["token"]))
        else:
            ok &= ck(False, "", "J1c **无法评估**：A 的隧道从未建成（与 J5 同一条前置理由）")

        # ── J5 负向对照：停掉靶机 A ⇒ 同一个隧道端口必须拿不到 A 的 token ──
        # ★★ 这一条**必须挂在前置正判据上**（本批次踩到）：
        #   若 A 的隧道压根没建成，`probe(LPORT_A)` 返回 b"" ≠ token ⇒ J5 **无条件报绿**。
        #   那是「负向观测」家族的变种：**空值即通过**。没有判别力的绿比红更危险。
        if not (data_ok["A"] and "lport" in tgt["A"]):
            ok &= ck(False,
                     "",   # 不会走到
                     "J5 **无法评估**：前置正判据未成立（A 的隧道 %s，数据面 %s）"
                     " ⇒ 此时「拿不到 token」是必然的、与实现无关，报绿即假绿"
                     % ("已登记" if "lport" in tgt["A"] else "未登记",
                        "通" if data_ok["A"] else "不通"))
        else:
            tgt["A"]["p"].kill()
            tgt["A"]["p"].wait(timeout=10)
            time.sleep(1)
            got = probe(LPORT_A, timeout=8)
            ok &= ck(got != tgt["A"]["token"],
                     "J5 负向对照：靶机 A 已停 ⇒ 连 :%d 拿不到 A 的 token（%r）"
                     " ⇒ J3 的绿不是本地回声" % (LPORT_A, got),
                     "J5 负向对照失败：靶机 A 都停了，连 :%d 仍拿回 A 的 token ⇒ "
                     "J3 的绿可能来自本地回声，判据无判别力" % LPORT_A)

        # ── J6 收尾：停隧道 ⇒ 端口必须释放 ─────────────────────────────────
        for d in tunnels:
            for path in ("/tunnel/stop", "/tunnel/delete"):
                E.call(path, {"p_tunnel_id": d["tid"]}, tok, timeout=60)
        time.sleep(2)
        for d in tunnels:
            owner = listen_owner(d["lport"])
            ok &= ck(owner is None or str(owner) != agent_os_pid,
                     "J6 隧道 :%d 停止后 agent 不再监听" % d["lport"],
                     "J6 隧道 :%d 停止后 pid=%s 仍在监听（agent pid=%s）"
                     % (d["lport"], owner, agent_os_pid))
        return ok
    finally:
        for name, d in tgt.items():
            try:
                d["p"].kill()
            except Exception:
                pass
        if not keep and tunnels:
            for d in tunnels:
                for path in ("/tunnel/stop", "/tunnel/delete"):
                    try:
                        E.call(path, {"p_tunnel_id": d["tid"]}, tok, timeout=30)
                    except Exception:
                        pass
        print("%s 收尾：靶机已停；agent 见 /agent/list（--keep 可保留现场）" % INFO)


# ── 已知开放项登记 ───────────────────────────────────────────────────────────
# ★ 登记项**照常打印 [BAD]**，只是不把整个套件判死。降级为「观察项」才是洗绿。
# ★ 每条登记都必须写清「为什么本批次不修」和「已做了什么补偿」，否则它会烂成万能借口。
KNOWN_OPEN = {
    "J6": (
        "上游协议级缺失：隧道 stop 时 core **无法通知 agent** 关监听。三层都断——"
        "① axc2@v1.2.0 的 adaptix.TunnelCallbacks 只有 Close(channelId)，**没有**「关监听」成员；"
        "② TsTunnelStop 只关服务端的 tunnel.listener（反向的监听在 agent 上⇒对 REVERSE 是空操作）；"
        "③ 上游留的 TsTunnelStopRportfwd 全树零调用者，且把 TunnelId 当 channelId 传（实参语义错）。"
        "要真修须改 axc2 模块 + core + **全部 20 个 extender 与 server 一起重建**"
        "（Go plugin 有包哈希校验）⇒ 本批次明确不做。"
        "补偿：agent 侧同端口再启用时会关掉**自己遗留的** listener（判据 J1c 覆盖该路径），"
        "外来占用者不受影响（判据 J1b 覆盖）。"
    ),
}


def classify():
    """返回 (真红, 已登记开放项)。认不出的标签算真红。"""
    real, opened = [], []
    for tag, msg in FAILS:
        (opened if tag in KNOWN_OPEN else real).append((tag, msg))
    return real, opened


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listener", default=None)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--target", action="store_true")     # 内部：靶机模式
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--token", default="X")
    a = ap.parse_args()
    if a.target:
        return target_mode(a.port, a.token)
    ok = run(a.listener, a.keep)
    real, opened = classify()
    tags_o = sorted(set(t for t, _ in opened))
    if not real and opened:
        print("\n%s 真红 0 条；已登记开放项 %d 条：%s" % (INFO, len(opened), tags_o))
        for t in tags_o:
            print("        [登记 %s] %s" % (t, KNOWN_OPEN[t]))
        print("\nRESULT: PASS（真判据全绿；含 %d 条**已登记开放项** %s —— 上面仍以 %s 打印，未隐藏）"
              % (len(opened), tags_o, BAD))
        return 0
    if ok and not opened:
        # ★ 过期检测：登记项本该红却是绿的 ⇒ 登记的「事实」与代码真相脱节，必须回来看
        print("\n%s 登记项 %s 本次**没有红** ⇒ 上游可能已修或行为已变，请复核并撤下登记"
              % (INFO, sorted(KNOWN_OPEN.keys())))
    print("\nRESULT: %s" % ("PASS（J1-J6 全绿）" if ok else "FAIL（见上面 BAD 行）"))
    return 0 if ok else 1


def entry():
    """★ 自动化框架只认 `RESULT:`。崩在中间时留一堆 traceback 而没有 RESULT，
    在汇总里和「这一项没跑到」长得一模一样（「负向观测」家族，本项目登记过）。"""
    try:
        return main()
    except KeyboardInterrupt:
        print("\nRESULT: FAIL（被中断）")
        return 130
    except Exception:
        import traceback
        traceback.print_exc()
        print("\nRESULT: FAIL（脚本异常，见上方 traceback）")
        return 1


if __name__ == "__main__":
    sys.exit(entry())
