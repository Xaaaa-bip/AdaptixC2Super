#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 ★端到端验收（自动化）—— libp2p listener 是否真能收到 agent 注册。

    python3 l2p_e2e.py <listener> [<listener2> ...] [--timeout 150] [--keep]

对每个 listener：
  1. login 取 token
  2. 记 baseline：a_listener == <listener> 的条目数（**不能当判据**，只作旁证）
  3. /agent/generate → 落盘 /root/gp5probe/tmp/e2e/agent_<listener>.bin
  4. 分离式启动（start_new_session + stdin=/dev/null + 输出重定向）→ 拿到 pid
  5. 轮询 /api/agent/list，直到出现 a_listener == <listener> 的条目（≤timeout 秒）
  6. ★判据（全绿才 PASS）：
       ★不是残留    该 pid **不在 baseline 快照里** —— 这条才是"新注册"的证据
       session key   a_session_key 非空
       os/user       a_os_desc / a_username 非空
       agent 存活    os.kill(pid, 0) 仍然成功
       传输身份      按 l_protocol 分支：libp2p ⇒ a_external_ip 是 peer id
                     （实测前缀 12D3KooW）；tcp ⇒ a_external_ip 是 IP 字面量
  7. kill agent（--keep 则留着）

★ 为什么不拿"列表里有没有"当判据：rig 是长跑实例，历史注册会残留
  （本阶段已被这条坑过一次：看到 agent 上线，其实是上一轮的记录）。

★★ M7 修复（批次3，2026-09-16）—— 本脚本原有三处**判据失效**，全是实测出来的：
  ① **两条重言式**：原判据里 `a_pid == 我刚启动的 pid` 与 `a_listener == 我要求的
     listener` —— 这两条正是**循环挑选 `hit` 的条件本身**，命中之后必然成立，
     **永远不可能红**。它们看着像"最强的证据"，实际一个字都没验。
     真正能证"不是残留"的是**独立于选择过程的第二份观测**：baseline 快照。
  ② **空值即通过**：`not (a_external_ip or "").replace(".","").isdigit()` —— 字段
     **缺失/为空**时 `"".isdigit()` 是 False，取反后 **PASS**。观测通道断了却报绿，
     正是本项目归档的"负向观测必须先证明通道有效"。
  ③ **尺寸当传输代理量**：`kind = "libp2p" if size > 15MB else "tcp"` —— 产物大小
     只是**代理量**，而且它还是**开关**：判错 ⇒ 整个传输身份检查被静默跳过。
     改为读 `/listener/list` 的 `l_protocol`（实测取值 `libp2p` / `tcp`）；查不到
     该 listener 就 **FAIL**（不许"查不到就默认 tcp"地静默降级）。
★ 为什么不用 shell 的 `setsid ... &` 而是 Popen(start_new_session=True)：
  直接拿到 pid，判据 4 才成立；顺带避免 ssh 通道被后台进程的 fd 挂住。
★ scratch 一律 /root/gp5probe/**（/tmp 会触发 p17 T6 的时间窗假红）。
"""
import json
import os
import ssl
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

BASE = "https://127.0.0.1:39012/api"   # 探针 rig；--port 38212 可指向运行态实例
CTX = ssl._create_unverified_context()
WORK = "/root/gp5probe/tmp/e2e"

BUILD = {
    "os": "linux",
    "arch": "amd64",
    "format": "elf",
    "reconn_timeout": "5s",
    "reconn_count": 5,
}

OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"


def call(path, obj=None, tok=None, method="POST", timeout=900):
    data = json.dumps(obj).encode() if obj is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    if tok:
        req.add_header("Authorization", "Bearer " + tok)
    try:
        with urllib.request.urlopen(req, context=CTX, timeout=timeout) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except Exception as e:
        return -1, "%s: %s" % (type(e).__name__, e)


CRED = "/root/gp5probe/.cred"     # 0600，内容就是口令本身；不在源码里留明文


def password():
    """口令来源：$L2P_PASS → /root/gp5probe/.cred。**源码里不留明文**。

    ★ 审计 S11：原版把口令硬编码在本文件里，而这个文件属于**交付树**
      （extenders/gopher_listener_tcp/patches/）。凭据不该跟着产物走。
      取不到就**明确失败**，不静默用空口令（那会变成一次谁也看不懂的 401）。
    """
    v = os.environ.get("L2P_PASS")
    if v:
        return v
    try:
        with open(CRED, encoding="utf-8") as f:
            v = f.read().strip()
    except OSError as e:
        print("%s 取不到口令：$L2P_PASS 未设，且读不到 %s（%s）"
              % (BAD, CRED, e))
        print("       修：printf '%%s' '<password>' > %s && chmod 600 %s" % (CRED, CRED))
        return None
    if not v:
        print("%s %s 是空的" % (BAD, CRED))
        return None
    return v


def login():
    pw = password()
    if pw is None:
        return None
    st, body = call("/login", {"username": "central", "password": pw,
                               "only_password": True})
    if st != 200:
        print("%s login -> %s %s" % (BAD, st, body[:300]))
        return None
    tok = json.loads(body).get("access_token", "")
    if not tok:
        print("%s login 回了 200 但没有 access_token: %s" % (BAD, body[:300]))
        return None
    return tok


def agents(tok):
    st, body = call("/agent/list", None, tok, "GET", timeout=60)
    if st != 200:
        print("%s /agent/list -> %s %s" % (BAD, st, body[:200]))
        return None
    try:
        v = json.loads(body)
    except Exception as e:
        print("%s /agent/list 不是 JSON: %s %s" % (BAD, e, body[:200]))
        return None
    return v if isinstance(v, list) else []


def listener_protocol(tok, name):
    """从 /listener/list 取 `l_protocol` —— **权威**的传输类型（实测取值 `libp2p`/`tcp`）。

    返回 (protocol, err)。查不到该 listener ⇒ err 非空 ⇒ 调用方必须 **FAIL**：
    "查不到"和"是 tcp"是两件事，把前者当成后者就是静默降级（观测通道失效）。
    """
    st, body = call("/listener/list", None, tok, "GET", timeout=60)
    if st != 200:
        return None, "/listener/list -> %s %s" % (st, body[:200])
    try:
        v = json.loads(body)
    except Exception as e:
        return None, "/listener/list 不是 JSON: %s %s" % (e, body[:200])
    v = v if isinstance(v, list) else []
    names = []
    for x in v:
        if not isinstance(x, dict):
            continue
        names.append(x.get("l_name"))
        if x.get("l_name") == name:
            p = x.get("l_protocol")
            if not p:
                return None, "%s 的 l_protocol 是空的（字段名变了？keys=%s）" \
                    % (name, sorted(x.keys()))
            return p, None
    return None, "列表里没有 l_name=%s 的条目（现有：%s）" % (name, names)


def generate(tok, listener, out):
    t0 = time.time()
    st, body = call("/agent/generate",
                    {"agent": "gopher", "listener_name": [listener],
                     "config": json.dumps(BUILD)}, tok)
    dt = time.time() - t0
    if st != 200:
        print("%s generate(%s) -> %s %s" % (BAD, listener, st, body[:400]))
        return None, dt
    j = json.loads(body)
    if not j.get("ok"):
        print("%s generate(%s) ok=false: %s" % (BAD, listener, str(j.get("message"))[:500]))
        return None, dt
    name_b64, _, content_b64 = j["message"].partition(":")
    raw = __import__("base64").b64decode(content_b64)
    with open(out, "wb") as f:
        f.write(raw)
    os.chmod(out, 0o755)
    return len(raw), dt


def run_one(tok, listener, timeout, keep):
    print("\n=== e2e: %s ===" % listener)
    os.makedirs(WORK, exist_ok=True)
    ag = agents(tok)
    if ag is None:
        return False
    # baseline 是**独立于选择过程**的第二份观测 ⇒ 它才是"不是残留"的真判据。
    base_pids = set()
    for a in ag:
        if a.get("a_listener") == listener and a.get("a_pid"):
            base_pids.add(str(a.get("a_pid")))
    print("%s baseline: a_listener=%s 已有 %d 条，pid 快照 %s"
          % (INFO, listener, len(base_pids), sorted(base_pids) or "（空）"))

    # ★ 传输类型读**权威字段**，不拿产物大小推（原版 `size>15MB ⇒ libp2p` 是代理量，
    #   而且它同时是**开关**：推错 ⇒ 传输身份检查被静默跳过）。
    proto, perr = listener_protocol(tok, listener)
    if perr:
        print("%s 取不到 %s 的传输类型：%s" % (BAD, listener, perr))
        return False
    print("%s listener %s 的 l_protocol=%s（权威字段）" % (INFO, listener, proto))

    out = os.path.join(WORK, "agent_%s.bin" % listener)
    log = os.path.join(WORK, "agent_%s.log" % listener)
    size, dt = generate(tok, listener, out)
    if size is None:
        return False
    print("%s generate 成功: %d B 耗时 %.1fs" % (INFO, size, dt))

    lf = open(log, "wb")
    p = subprocess.Popen([out], stdin=subprocess.DEVNULL, stdout=lf,
                         stderr=subprocess.STDOUT, start_new_session=True, cwd=WORK)
    lf.close()
    print("%s agent 已启动 pid=%d  log=%s" % (INFO, p.pid, log))

    # ★ S12 修复：**从启动那一刻起**所有退出路径都必须回收进程。
    #   原版有三条 `return False` 直接绕过 kill ⇒ agent 泄漏，而泄漏的 agent 会
    #   **污染下一轮 baseline**（下一轮看到"已存在"就更容易误判），是自伤型的。
    try:
        t0, hit = time.time(), None
        while time.time() - t0 < timeout:
            if p.poll() is not None:
                print("%s agent 提前退出 rc=%s（%.1fs）—— 见 %s"
                      % (BAD, p.returncode, time.time() - t0, log))
                print(open(log, errors="replace").read()[:1500])
                return False
            ag = agents(tok)
            if ag is None:
                print("%s 轮询中断：/agent/list 取不到（判据不可判 ⇒ 按失败处理）" % BAD)
                return False
            for a in ag:
                if a.get("a_listener") == listener and a.get("a_pid") == str(p.pid):
                    hit = a
                    break
            if hit:
                break
            time.sleep(3)

        if not hit:
            print("%s %.0fs 内未出现 a_listener=%s 且 a_pid=%d 的条目"
                  % (BAD, timeout, listener, p.pid))
            tail = [a for a in (agents(tok) or []) if a.get("a_listener") == listener]
            print("      同 listener 的现存条目 pid=%s" % [a.get("a_pid") for a in tail])
            print(open(log, errors="replace").read()[:1500])
            return False
        return judge(p, hit, listener, proto, base_pids, time.time() - t0)
    finally:
        if not keep:
            stop(p)


def stop(p):
    try:
        os.kill(p.pid, signal.SIGKILL)
        time.sleep(1)
        print("%s 已停掉 agent pid=%d" % (INFO, p.pid))
    except Exception as e:
        print("%s kill pid=%d 失败: %s" % (INFO, p.pid, e))


def judge(p, hit, listener, proto, base_pids, dt):
    """判据区（与进程生命周期解耦，便于单测与阅读）。"""
    alive = True
    try:
        os.kill(p.pid, 0)
    except Exception:
        alive = False
    pid = str(hit.get("a_pid"))
    ext = hit.get("a_external_ip") or ""
    checks = [
        # ★ M7 修复①：这里原来放的是 `a_pid == p.pid` 与 `a_listener == listener` ——
        #   而那两条**正是选出 hit 的条件本身**，命中后必然为真，**永远不可能红**。
        #   换成真正独立的一份观测：baseline 快照（在 agent 启动**之前**取的）。
        ("该 pid 不在 baseline 里（证明是新注册，不是残留）",
         bool(pid) and pid not in base_pids),
        ("a_session_key 非空", bool(hit.get("a_session_key"))),
        ("a_os_desc 非空", bool(hit.get("a_os_desc"))),
        ("a_username 非空", bool(hit.get("a_username"))),
        ("agent 进程仍存活", alive),
    ]
    # ★ M7 修复②：原来是 `not ext.replace(".","").isdigit()` —— ext **为空**时
    #   `"".isdigit()` 是 False，取反后 **PASS**：字段丢了反而报绿。
    #   现在两个方向都是**正向**断言，且值域**实测**自 rig（15 个样本）：
    #     libp2p ⇒ 12D3KooW… 的 peer id（52 字符）；tcp ⇒ 127.0.0.1 这样的 IP 字面量
    if proto == "libp2p":
        checks.append(("libp2p 的外部身份是 peer id（实测前缀 12D3KooW）",
                       ext.startswith("12D3KooW") and len(ext) == 52))
    elif proto == "tcp":
        checks.append(("tcp 的外部身份是 IP 字面量",
                       bool(ext) and all(c.isdigit() or c == "." for c in ext)))
    else:
        checks.append(("未知传输 %r ⇒ 没有可信判据，按失败处理" % proto, False))
    for nm, ok in checks:
        print("%s %s" % (OK if ok else BAD, nm))
    print("%s 上线耗时 %.0fs  a_id=%s  ip=%s  os=%s  user=%s  pid=%s"
          % (INFO, dt, hit.get("a_id"), ext, hit.get("a_os_desc"),
             hit.get("a_username"), pid))
    return all(ok for _, ok in checks)


def show_listeners(tok):
    st, body = call("/listener/list", None, tok, "GET", timeout=60)
    if st != 200:
        print("%s /listener/list -> %s %s" % (BAD, st, body[:300]))
        print("RESULT: FAIL")
        return 1
    v = json.loads(body)
    v = v if isinstance(v, list) else []
    print("%s /api/listener/list 共 %d 条" % (INFO, len(v)))
    # ★ 原样打印，不猜字段名（本脚本第一版按 name/type/config 猜，全是 None —— 与
    #   「按推测的结构解析」同族：解析器猜错时输出的是"空"，与"真没有"同形）。
    for i, x in enumerate(v):
        if i == 0 and isinstance(x, dict):
            print("  keys=%s" % sorted(x.keys()))
        print("  %s" % json.dumps(x, ensure_ascii=False)[:500])
    print("RESULT: PASS（仅列出）")
    return 0


def show_list(tok):
    ag = agents(tok)
    if ag is None:
        print("RESULT: FAIL")
        return 1
    print("%s /api/agent/list 共 %d 条" % (INFO, len(ag)))
    for a in ag:
        print("  listener=%-8s pid=%-8s id=%-9s os=%-14s ip=%s"
              % (a.get("a_listener"), a.get("a_pid"), a.get("a_id"),
                 a.get("a_os_desc"), a.get("a_external_ip")))
    print("RESULT: PASS（仅列出）")
    return 0


def main():
    global BASE
    # ★ 参数要**吃掉选项的值**：早先写成 `[a for a in argv if not a.startswith("--")]`
    #   会把 `--timeout 150` 的 **150** 当成第三个 listener 名 ⇒ 汇总行打出
    #   `2/3 PASS`（真跑的两个都 PASS，却因为多出一个假 listener 而变红）。
    #   同族于「判据自己算错」：断言数被自己的解析器改掉了。
    argv = sys.argv[1:]
    listeners, timeout, keep, port, do_list, i = [], 150, False, None, False, 0
    while i < len(argv):
        a = argv[i]
        if a == "--timeout":
            timeout = int(argv[i + 1])
            i += 2
            continue
        if a == "--port":
            port = int(argv[i + 1])
            i += 2
            continue
        if a == "--keep":
            keep = True
            i += 1
            continue
        if a == "--list":
            do_list = True
            i += 1
            continue
        if a == "--listeners":
            do_list = "listeners"
            i += 1
            continue
        if a.startswith("--"):
            print("%s 未知参数 %s" % (BAD, a))
            return 2
        listeners.append(a)
        i += 1
    if port:
        BASE = "https://127.0.0.1:%d/api" % port
    if not listeners and not do_list:
        print("用法: %s <listener> [<listener2> ...] [--timeout 150] [--keep] [--port 39012]"
              " [--list] [--listeners]" % sys.argv[0])
        print("RESULT: FAIL（参数不足）")
        return 2
    tok = login()
    if not tok:
        print("RESULT: FAIL（login 失败）")
        return 2
    print("%s login ok @ %s, token len %d" % (INFO, BASE, len(tok)))
    if do_list == "listeners":
        return show_listeners(tok)
    if do_list:
        return show_list(tok)
    res = [(ls, run_one(tok, ls, timeout, keep)) for ls in listeners]
    print("\n===== 汇总 =====")
    for ls, ok in res:
        print("%s %s" % ("PASS" if ok else "FAIL", ls))
    bad = sum(1 for _, ok in res if not ok)
    print("RESULT: %d/%d PASS" % (len(res) - bad, len(res)))
    return 0 if bad == 0 else 1


def entry():
    """★ S12 修复：**任何**异常都必须留下 `RESULT:` 行。

    自动化框架只认 `RESULT:` —— 崩在中间时留一堆 traceback 而没有 RESULT，
    在汇总里和"没跑到"是同一副样子（本项目归档的"负向观测"家族）。
    """
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
