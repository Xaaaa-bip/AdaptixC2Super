#!/usr/bin/env python3
"""Gate 19 —— JA4 不退化：把 Phase 1b 的一次性实测变成**能红也能绿**的常态门。

判据（§8 Gate 19）：agent→TestWSS 的 ClientHello 算出的 JA4.1 必须**逐位等于**
`tools/ja4_baseline.txt` 里 curl/OpenSSL 的基线 `t13d3112h2_e8f1e7e78f70_b26ce05bbdd6`。

为什么需要这条门（三条都是实测出来的，不是推测）：
  1. **A′ 已经做完、且运行态生效**（§4.1）：书写器 `src_agent/mbedtls/library/ssl_client.c`
     的 3 个 `MBEDTLS_AX_JA4` 块 + 开关 `mbedtls_config_agent.h:2230`；agent 构建走
     `pl_main.go:486 NativeMbedtlsSrcDir="src_agent/mbedtls/library"` 与
     `:499 -DMBEDTLS_CONFIG_FILE='<mbedtls_config_agent.h>'`（相对路径 ⇒ 运行期编
     `run/extenders/linux_agent/src_agent/mbedtls/`）。
  2. **它没有任何自动化门**：`gate4.sh:35` 那行 `run "gap12 JA4 harness" bash
     tools/ja4_harness.sh` **漏了参数** ⇒ 走 usage 分支 `exit 2` ⇒ **恒 FAIL**（ISS-093，
     死门）；`run_regress.sh` 里根本没有这一项。
  3. **它很容易被无声改坏**：谁动 mbedtls / `ws_connector.c` / 构建参数，检测面就退化，
     而退化的表现是"什么都没有发生"—— 这正是最需要门的一类改动。

★★ 本脚本的核心不是"跑一次 verify"，而是**让门自证能红**（ISS-083 / ISS-093）：
   先抓一条**非 curl 形状**的 ClientHello（python-ssl，不带 ALPN）⇒ 比较器**必须判 FAIL**。
   比较器"永远返回通过"是这类门最典型的失效形态；不先证明它能红，那个绿就不值钱。

★ 副作用面：只碰 **TestWSS(127.0.0.1:8443)** 一条会话 + `lo` 抓包；scratch 全在
  `tools/.scratch/`（**不落 `/tmp`** —— 免得与 `p17` T6 的时间窗纠缠，见问题归档）。
  FAIL 时保留 pcap 供取证，PASS 时自行收尸。
"""
import json
import os
import re
import shutil
import socket
import sqlite3
import ssl
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e2e

LISTENER = "TestWSS"
DB = "/root/cc/run/data/adaptixserver.db"
HARNESS = os.path.join(HERE, "ja4_harness.sh")
BASELINE_FILE = os.path.join(HERE, "ja4_baseline.txt")
SCRATCH = os.path.join(HERE, ".scratch")
CAP_SECS = 30           # 抓包窗口：要盖住"agent 起 → 注册"这段（beacon 间隔内）
FALLBACK_PORT = 8443

R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-52s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def baseline():
    for line in open(BASELINE_FILE, encoding="utf-8", errors="replace"):
        if line.startswith("curl-openssl:"):
            return line.split("JA4.1 =")[1].split()[0]
    return None


def listen_port():
    """从运行期 DB 取 TestWSS 的 port_bind（拿不到就退回 8443）。

    ★ 为什么要读 DB 而不是写死：抓包过滤器要收窄到**这一个端口**，否则窗口里
      任何残留 agent 的重连都会污染 pcap —— 阴性对照会因此假绿。
    """
    try:
        con = sqlite3.connect("file:%s?mode=ro" % DB, uri=True, timeout=5)
        row = con.execute("select ListenerConfig from Listeners where ListenerName=?",
                          (LISTENER,)).fetchone()
        con.close()
        if row:
            return int(json.loads(row[0]).get("port_bind") or FALLBACK_PORT), "DB"
    except Exception as e:
        return FALLBACK_PORT, "DB 读取失败(%s)" % type(e).__name__
    return FALLBACK_PORT, "DB 无该 listener"


def capture(port, tag):
    """起 tshark 抓 lo:<port> 的 TCP，按 duration 自行退出。返回 (Popen, pcap)。"""
    pcap = os.path.join(SCRATCH, "ja4_%s.pcap" % tag)
    try:
        os.remove(pcap)
    except OSError:
        pass
    p = subprocess.Popen(["tshark", "-i", "lo", "-f", "tcp port %d" % port,
                          "-w", pcap, "-a", "duration:%d" % CAP_SECS],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return p, pcap


def verify(pcap):
    """复用 ja4_harness.sh 的 verify（**比较器只有这一份实现**）。"""
    r = subprocess.run(["bash", HARNESS, "verify", pcap],
                       capture_output=True, text=True, timeout=90)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def agent_ja4(out):
    m = re.search(r"agent\s+JA4\.1 = (\S+)", out or "")
    return m.group(1) if m else None


def main():
    os.makedirs(SCRATCH, exist_ok=True)
    base = baseline()
    rec("T0 基线可读（ja4_baseline.txt 的 curl-openssl 行）", bool(base),
        "baseline JA4.1 = %s" % base)
    miss = [t for t in ("tshark",) if shutil.which(t) is None]
    rec("T0b 仪器齐备", not miss, "缺: %s" % (miss or "无"))
    port, src = listen_port()
    print("[p31] %s = 127.0.0.1:%d（port 来源：%s）" % (LISTENER, port, src), flush=True)
    if not base or miss:
        f = sum(1 for x in R if not x)
        print("\nPASS=%d FAIL=%d" % (len(R) - f, f), flush=True)
        print("RESULT: FAIL", flush=True)
        return 1

    pid = 0
    keep = []
    try:
        # ── C0 阴性对照：换客户端（python-ssl，无 ALPN）⇒ 比较器**必须**判 FAIL ──
        p0, pcap_n = capture(port, "neg")
        time.sleep(1.0)
        try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            s = ctx.wrap_socket(socket.create_connection(("127.0.0.1", port), timeout=4),
                                server_hostname="ja4-neg.invalid")
            s.close()
        except Exception as e:
            # 连不上/握不上都是**预期**：我们要的只是它把 ClientHello 发出去
            print("[p31] 阴性对照客户端结束：%s" % type(e).__name__, flush=True)
        p0.wait(timeout=CAP_SECS + 20)
        rc_n, out_n = verify(pcap_n)
        last = [x for x in (out_n or "").strip().splitlines() if x.strip()]
        # ★★ 必须把「比对不上」与「压根没抓到/比较器报错」分开：两者都 rc≠0，
        #    但后者是**瞎红** —— 它对"比较器有没有判别力"零信息，却是阴性对照最
        #    容易滑进去的假绿形态（ISS-083 / ISS-093 同族：**红要红在对的理由上**）。
        nohello = "no ClientHello" in (out_n or "")
        rec("C0 ★阴性对照：非 curl 形状的 ClientHello ⇒ 比较器判 **mismatch**",
            rc_n != 0 and not nohello,
            "rc=%s nohello=%s | %s" % (rc_n, nohello, (last[-1][:70] if last else "无输出")))
        if rc_n == 0 or nohello:
            keep.append(pcap_n)      # 比较器恒真 或 抓包为空 ⇒ 两种都留物证

        # ── C1 阳性：agent→TestWSS 的 ClientHello ⇒ 必须 == 基线 ──
        binp = e2e.gen_agent(LISTENER, "p31")
        p1, pcap_p = capture(port, "agent")
        time.sleep(0.8)
        pid = e2e.launch(binp, "p31")
        aid = e2e.wait_registered(LISTENER, pid, timeout=CAP_SECS + 40)
        rec("T1 agent 经 %s 上线（证明抓包窗口里有真会话）" % LISTENER, bool(aid),
            "aid=%s pid=%d" % (aid, pid))
        p1.wait(timeout=CAP_SECS + 30)
        rc_p, out_p = verify(pcap_p)
        got = agent_ja4(out_p)
        rec("T2 抓到 ClientHello 并算出 JA4.1（否则 T3 是空跑）", bool(got),
            "agent JA4.1 = %s" % (got or "无"))
        rec("T3 ★ JA4.1 == curl/OpenSSL 基线（逐位）",
            rc_p == 0 and bool(got) and got == base,
            "agent=%s baseline=%s rc=%s" % (got or "?", base, rc_p))
        if rc_p != 0 or got != base:
            keep.append(pcap_p)
    except Exception as e:
        # ★★ 台面自己出事（`e2e.wait_registered` 是**抛 TimeoutError**，不是返回
        #    None）绝不能被算成 PASS。旧写法把汇总 `return` 放在 `finally` 里 ——
        #    **`return` 在 `finally` 中会吞掉异常**，于是"T1/T2/T3 根本没跑"会打印成
        #    `PASS=3 FAIL=0 / RESULT: PASS`。那正是本脚本要防的那类假绿，不能自己犯。
        rec("T4 ★ 台面跑完（未被异常中断）", False,
            "%s: %s" % (type(e).__name__, str(e)[:70]))
    finally:
        if pid:
            try:
                e2e.kill(pid)
            except Exception as e:
                print("[warn] kill agent: %s" % e, flush=True)
        for f in os.listdir(SCRATCH):
            if f.startswith("ja4_") and f.endswith(".pcap") \
                    and os.path.join(SCRATCH, f) not in keep:
                try:
                    os.remove(os.path.join(SCRATCH, f))
                except OSError:
                    pass
        if keep:
            print("[p31] 留证 pcap: %s" % ", ".join(keep), flush=True)

    # 汇总放在 try/finally **之外**：异常不再被 return 吞掉
    EXPECT = 6          # T0 / T0b / C0 / T1 / T2 / T3
    if len(R) < EXPECT:
        rec("T5 ★ 判据自证跑满 %d 条（没被静默跳过）" % EXPECT, False, "实收 %d 条" % len(R))
    f = sum(1 for x in R if not x)
    print("\nPASS=%d FAIL=%d" % (len(R) - f, f), flush=True)
    print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"), flush=True)
    return 0 if f == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
