#!/usr/bin/env python3
"""gap7_bof_fuzz.py — Phase 4 缺口 7：18 个 BOF × 3 组参数韧性（空/垃圾/超长）。

验收（LIVE_TEST_CHECKLIST.md）：
  每次都有 CALLBACK_ERROR 回显或正常输出，agent 不崩。

安全设计（本脚本会真跑有副作用的 BOF）：
  - 运行前起看门狗：sleep 900 后无条件清理残留（防会话中断留下脏状态）
  - 破坏性 BOF 逐项测完后立即定向清理，并在末尾统一复验
  - 健康检查走 /agent/list 的 a_last_tick（不消耗 beacon 往返）
用法: gap7_bof_fuzz.py [--listener TestHTTP] [--only name1,name2]
"""
import glob, json, os, sqlite3, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e

BOF_DIR = "/root/cc/run/extenders/linux_agent/bofs"

# 18 个 BOF，按破坏性排序：只读的先跑，写盘的最后跑
READONLY = ["container_detect", "cred_harvest", "host_recon", "kernel_exploit_check",
            "ld_preload_check", "net_enum", "proc_enum", "service_enum",
            "shadow_dump", "ssh_keys", "suid_scan", "memfd_exec", "ebpf_unload"]
DESTRUCTIVE = ["ko_load", "ebpf_load", "ssh_persist", "cron_persist", "ld_preload_install"]

# 4 组变体，覆盖**两条参数通道**（缺一不可）：
#   empty   —— 无参数
#   plain   —— 空格分词 → base64 解码失败 → 走明文回退分支（ISS-005 的回归用例）
#   garbage —— "AAAAAAAA" 是**合法 base64**（解出 6 个 0 字节）→ 走 base64 通道
#   overlong—— 同上，4096 个 'A' → 3072 个 0 字节，测大载荷
# 注意：不能用 "@@@" 这类当"垃圾"，那只是另一种明文；要区分的是**通道**，不是字符集。
VARIANTS = [("empty", ""), ("plain", "a b c"),
            ("garbage", "AAAAAAAA"), ("overlong", "A" * 4096)]

AK = "/root/.ssh/authorized_keys"
AK_BAK = "/tmp/gap7_authorized_keys.bak"
# ★★ 看门狗脚本与日志放**源码树的 `.scratch/`**，不放 /tmp（09-16 迁）。
# 理由：p17 T6 的判据是 `find /dev/shm /tmp /var/tmp /run -newermt @<agent启动时刻>`
# —— 它罚的是「**落进 agent 运行窗口内**的写入」，不是「/tmp 里有文件」。
# 而看门狗是 `sleep 900` 之后才写日志的：只要下一批回归在本轮 gap7 结束后 900s 内
# 启动，那行 append 就**正好落在 p17 的观测窗口里** ⇒ 假红，且症状出现在**别的**
# 台面上、与本事无关（这类跨台面假红最难查）。
# `.scratch/` 在开发工作树内，T6 已整体排除（见 p17 的 WL 注释）。
# 这与既有约束"scratch 一律放源码树 `.scratch/`"是同一条。
_SCRATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".scratch")
WATCHDOG = os.path.join(_SCRATCH, "gap7_watchdog.sh")
WATCHDOG_LOG = os.path.join(_SCRATCH, "gap7_watchdog.log")

# ★★★ ssh_persist 的目标文件覆盖（09-16 加）。
# 背景：`ssh_persist` 现在收「可选第二参数 = 目标路径」—— 理由不是"为了好测"，而是
# `sshd_config` 的 `AuthorizedKeysFile` **本来就可配**（Debian 系
# `/etc/ssh/authorized_keys/%u`），写死路径的持久化 BOF 本身是残的。
# 台面**必须**借它把写入指到一次性文件：否则本 fuzz 的 4 组变体行会落进真实的
# `/root/.ssh/authorized_keys` —— 那是本机唯一能让我进来的凭据，也是安全约束里
# **唯一明令禁止做实验的文件**。`p27` 的 T8 把"真实文件 sha256 全程未变"本身
# 变成了判据；这里做的是同一件事的**前置预防**（不让它成为可能）。
#
# ★ 为什么不是"简单地在参数末尾追加路径"：服务器把 `execute bof` 的参数按
# **空格分词**成多个 msgpack 字段，而 `plain` 变体故意是 `a b c`（多词）⇒
# 追加的路径会落到第 4 个字段，而产品只读第 2 个 ⇒ 覆盖不到位、且**静默**。
# 所以对 ssh_persist 必须**保证变体是单 token**（`plain` 退化成它的第一个词，
# 明文回退这条性质本身保留 —— 那才是这个变体要测的东西）。
AK_TEST = "/root/.axgap7/authorized_keys"


def bof_args(bof, varg):
    """该 BOF 这一轮实际要发的参数字符串（不含 .o 路径）。"""
    if bof == "ssh_persist" and varg:
        return "%s %s" % (varg.split()[0], AK_TEST)
    return varg


# ---------- 看门狗 / 清理 ----------
WATCHDOG_SRC = """#!/bin/bash
# gap7 看门狗：无论测试如何结束，超时后强制清理残留
sleep 900
rm -f /etc/ld.so.preload
rm -f /etc/cron.d/sys-update
find /etc/cron.d -maxdepth 1 -name 'AAAA*' -delete 2>/dev/null
# ⚠ P8 起 pin 名会进 hide_names（ISS-062）→ `ls /sys/fs/bpf` **列不出被藏起来的 pin**。
#   本看门狗只做"兜底大扫除"，扫不到的会静默残留；真正负责清自己 pin 的是各套件
#   自己的 cleanup（它们按**显式路径**删）。这里保留 ls 只为清"别人留下的、没被藏"的项。
for p in $(ls /sys/fs/bpf 2>/dev/null); do bpftool link detach pinned /sys/fs/bpf/$p 2>/dev/null; rm -f /sys/fs/bpf/$p; done
echo "[gap7-watchdog] cleanup done at $(date -Is)" >> "%s"
""" % WATCHDOG_LOG


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout.strip()


def cleanup_all():
    sh("rm -f /etc/ld.so.preload /etc/cron.d/sys-update")
    sh("find /etc/cron.d -maxdepth 1 -name 'AAAA*' -delete 2>/dev/null")
    if os.path.exists(AK_BAK):
        sh("cp -f %s %s" % (AK_BAK, AK))
    sh("rm -f /tmp/AxMemfd*")
    # ★ ssh_persist 的目标文件覆盖（见 bof_args 注释）：整个目录一并清掉。
    sh("rm -rf /root/.axgap7")

def residue_report():
    r = {}
    r["ld.so.preload"] = sh("test -e /etc/ld.so.preload && echo PRESENT || echo clean")
    r["cron.d"] = sh("ls /etc/cron.d 2>/dev/null | tr '\\n' ' '") or "(empty)"
    r["authorized_keys_diff"] = sh(
        "diff -q %s %s >/dev/null 2>&1 && echo same || echo DIFF" % (AK_BAK, AK)) \
        if os.path.exists(AK_BAK) else "no-backup"
    # ⚠ 这个读数**已经不是有效判据**了：P8 之后被隐藏的 pin 在 `ls` 里看不见，
    #   "(empty)" 既可能是真干净、也可能是全被藏起来。gap7 自己不 pin，所以这里
    #   只当**粗筛**用；精确判定靠各套件的按显式路径复扫（gap10/p03/p18/p19/p21）。
    r["bpf_pins"] = sh("ls /sys/fs/bpf 2>/dev/null | tr '\\n' ' '") or "(empty)"
    r["bpf_progs"] = sh("bpftool prog list 2>/dev/null | grep -c '^[0-9]*:' || echo 0")
    return r


# ---------- console / 健康 ----------
def console_max_id():
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        m = con.execute("SELECT COALESCE(MAX(Id),0) FROM Consoles").fetchone()[0]
        con.close()
        return m
    except Exception:
        return 0


def console_since(aid, min_id):
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        rows = con.execute(
            "SELECT Id, CAST(Packet AS TEXT) FROM Consoles WHERE AgentId=? AND Id>? ORDER BY Id",
            (aid, min_id)).fetchall()
        con.close()
        return rows
    except Exception:
        return []


def tick_age(aid):
    """agent 心跳年龄（秒）；None = 已从列表消失（疑似崩溃）。"""
    try:
        agents = e2e._auth_http("GET", "/agent/list")
    except Exception:
        return None
    if not isinstance(agents, list):
        return None
    for a in agents:
        if a.get("a_id") == aid:
            return time.time() - a.get("a_last_tick", 0)
    return None


def alive(aid, pid, max_age=90):
    try:
        os.kill(pid, 0)
    except OSError:
        return False, "process gone"
    age = tick_age(aid)
    if age is None:
        return False, "not in agent list"
    if age > max_age:
        return False, "tick stale %.0fs" % age
    return True, "tick %.0fs" % age


# ---------- 主流程 ----------
def main():
    args = sys.argv[1:]
    listener = args[args.index("--listener") + 1] if "--listener" in args else "TestHTTP"
    only = args[args.index("--only") + 1].split(",") if "--only" in args else None

    # 看门狗（防会话中断）
    os.makedirs(_SCRATCH, exist_ok=True)
    with open(WATCHDOG, "w") as f:
        f.write(WATCHDOG_SRC)
    subprocess.Popen(["bash", WATCHDOG], stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)
    sh("cp -f %s %s" % (AK, AK_BAK))

    print("[gap7] listener=%s  bof_dir=%s" % (listener, BOF_DIR), flush=True)
    binp = e2e.gen_agent(listener, "gap7")
    pid = e2e.launch(binp)
    aid = e2e.wait_registered(listener, pid, timeout=90)
    print("[gap7] agent up  id=%s pid=%s" % (aid, pid), flush=True)

    order = READONLY + DESTRUCTIVE
    if only:
        order = [b for b in order if b in only]

    results = []          # (bof, variant, status, note)
    fatal = None
    try:
        for bof in order:
            obj = "%s/%s.o" % (BOF_DIR, bof)
            if not os.path.exists(obj):
                for vn, _ in VARIANTS:
                    results.append((bof, vn, "SKIP", "no .o"))
                continue
            base = console_max_id()
            sent = 0
            for vn, varg in VARIANTS:
                a = bof_args(bof, varg)
                cmd = "execute bof %s" % obj + ((" " + a) if a else "")
                try:
                    e2e.send_raw(aid, cmd)
                    sent += 1
                except Exception as e:
                    results.append((bof, vn, "SENDFAIL", str(e)[:40]))
            # 等 3 条新 console（一个 beacon 周期内批量执行）
            t0, got = time.time(), []
            while time.time() - t0 < 150 and len(got) < sent:
                got = console_since(aid, base)
                if len(got) >= sent:
                    break
                ok, why = alive(aid, pid, max_age=120)
                if not ok:
                    fatal = "%s (%s)" % (bof, why)
                    break
                time.sleep(2)
            if fatal:
                for vn, _ in VARIANTS:
                    results.append((bof, vn, "FATAL", fatal))
                break
            # 每条回显归类
            for i, (vn, _) in enumerate(VARIANTS[:sent]):
                if i < len(got):
                    txt = json.loads(got[i][1]).get("a_text", "")
                    tag = "ERR" if ("[-]" in txt or "CALLBACK_ERROR" in txt) else "OUT"
                    results.append((bof, vn, "OK", "%s %s" % (tag, txt.strip().replace("\n", " ")[:52])))
                else:
                    results.append((bof, vn, "NORESP", "no console row"))
            ok, why = alive(aid, pid, max_age=120)
            if not ok:
                fatal = "%s post-check (%s)" % (bof, why)
                break
            print("[gap7] %-22s %s" % (bof, why), flush=True)
            # 破坏性项：每项测完立即清理
            if bof in DESTRUCTIVE:
                cleanup_all()
                time.sleep(1)
    finally:
        if fatal:
            print("[gap7] !!! FATAL: %s" % fatal, flush=True)
        cleanup_all()
        try:
            e2e.kill(pid)
        except Exception:
            pass

    # ---------- 汇总 ----------
    print("\n===== gap7 结果 =====", flush=True)
    bad = [r for r in results if r[2] not in ("OK",)]
    for bof, vn, st, note in results:
        if st != "OK":
            print("  FAIL %-22s %-9s %-8s %s" % (bof, vn, st, note), flush=True)
    nbof = len(set(r[0] for r in results))
    print("BOF %d 个 / 用例 %d 条 / 异常 %d 条" % (nbof, len(results), len(bad)), flush=True)
    print("\n===== 残留复验 =====", flush=True)
    for k, v in residue_report().items():
        print("  %-20s %s" % (k, v), flush=True)
    # ★ 备份**只能在这里**删：cleanup_all() 用它恢复真实文件、residue_report() 用它
    #   做 diff 基线，两者都在上面。
    #   而它落在 **/tmp** —— 用户的既有约束写明"往 /tmp 放任何文件都会触发 p17 T6
    #   假红"，那条判据的内容正是"白名单外的目录里不许有我们的产物"。
    #   ⇒ 留着它就是**用本工具的残留去污染另一套台面**，而且症状出现在**别的**台面上、
    #   与本事无关，是最难查的一类假红。正常收尾 ⇒ 必须清干净。
    #   （会话被打断的那条路走不到这里 —— 那时 AK_BAK 是唯一能恢复真实文件的凭据，
    #     正应该留着。）
    try:
        os.remove(AK_BAK)
    except OSError:
        pass
    print("RESULT: %s" % ("PASS" if not bad and not fatal else "FAIL"), flush=True)
    sys.exit(0 if (not bad and not fatal) else 1)


if __name__ == "__main__":
    main()
