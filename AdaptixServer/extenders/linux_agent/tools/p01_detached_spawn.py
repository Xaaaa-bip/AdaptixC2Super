#!/usr/bin/env python3
"""p01_detached_spawn.py — P0-1 验收：spawn_detached.o（§3.1 / §3.2，门 S1）

被测对象：src_agent/bofs/spawn_detached.c
  父: 读载荷 → tmpfs 载体(AxTmpfsCarrier，写完即 unlink) → pipe → fork
  子: setsid → fork → exit(0)
  孙: 关 >2 的 fd（例外表留 载体+回退读端+管道写端）→ stdio→/dev/null → unlink(源)
      → 管道报 [4B pid][1B 0x01] → 关管道 → execve(/proc/self/fd/N) → 失败才 exit(3)
  父: 收 pid+状态 → waitpid(子) → close(载体) → 探 /proc/<孙pid> 判活 → 立即返回

断言（每条都要现场证据，不看回显自说自话）：
  A1 回执是**成功**文案 `[+] ... pid=N ... source unlinked ... verified`
  A2 源 ELF 消失（S1 磁盘零残留）
  A3 载荷在跑、非 agent 子进程（PPID != agent pid）、自成会话（PID == SID）
  A4 /proc/N/cmdline = "[kworker/0:1]"（默认 argv[0] 伪装）
  A5 载荷继承的 fd 仅 {0,1,2}（载体 fd CLOEXEC），**无任何 socket**（C2 连接未泄漏）
  A6 /proc/N/exe = <dir>/.m<8hex> (deleted)，且全程无 "memfd:"（P1-b）
  A6b 两次 spawn 的载体名不同（真随机，不是固定 IOC）
  A7 spawn 后 agent 未被拖住：tick age < 60s 且紧跟的 getuid 60s 内回执
  A8 调用方给 argv[0] 时按给的显示
  A9 --keep 保留源文件
  A10 路径不存在 → 错误回执，agent 不挂

★ 观测层复用 gap11_session_soak 的 Consoles 读法（同一套 task_id 配对，ISS-023）。
★ 第一版脚本的三个自伤，留档：A1 的正则 `pid=(\\d+)` 把 "died before arming" 也判成
  PASS；A5 把载荷自己 fopen 的输出文件当成"继承来的杂 fd"（载荷必须先拍 fd 快照再
  开文件）；A7 给 getuid 25s 预算，而 beacon 往返本就 10~25s。
"""
import json, os, re, sqlite3, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
from gap11_session_soak import _rows, max_id, tick_age

BOF = "/root/cc/run/extenders/linux_agent/bofs/spawn_detached.o"
PL, PL2, PLK = "/tmp/p01_payload", "/tmp/p01_payload2", "/tmp/p01_payload_keep"
OUT1, OUT2 = "/tmp/p01_ran.txt", "/tmp/p01_ran2.txt"

PAYLOAD_C = r"""
#include <stdio.h>
#include <unistd.h>
#include <string.h>
int main(int argc, char **argv) {
    const char *out = (argc > 1) ? argv[1] : "/tmp/p01_ran.txt";
    /* fd 快照必须在 fopen 之前拍 —— 之后开的文件是载荷自己的，不算继承 */
    char snap[16][256];
    int ns = 0;
    for (int i = 0; i < 64 && ns < 16; i++) {
        char p[64], t[256];
        snprintf(p, sizeof p, "/proc/self/fd/%d", i);
        ssize_t k = readlink(p, t, sizeof t - 1);
        if (k < 0) continue;
        t[k] = 0;
        snprintf(snap[ns], sizeof snap[0], "fd%d=%s", i, t);
        ns++;
    }
    FILE *f = fopen(out, "w");
    if (!f) return 9;
    fprintf(f, "argv0=%s\npid=%d\nppid=%d\nsid=%d\npgid=%d\n", argv[0], (int)getpid(),
            (int)getppid(), (int)getsid(0), (int)getpgrp());
    for (int i = 0; i < ns; i++) fprintf(f, "%s\n", snap[i]);
    char ex[256];
    ssize_t ke = readlink("/proc/self/exe", ex, sizeof ex - 1);
    if (ke > 0) { ex[ke] = 0; fprintf(f, "exe=%s\n", ex); }
    FILE *m = fopen("/proc/self/maps", "r");   /* 载体路径：应为 tmpfs "(deleted)"，非 memfd */
    char line[512];
    int shown = 0;
    while (m && fgets(line, sizeof line, m) && shown < 10)
        if (strstr(line, "deleted") || strstr(line, "memfd")) { fprintf(f, "map=%s", line); shown++; }
    if (m) fclose(m);
    fclose(f);
    sleep(60);
    return 0;
}
"""

results = []


def rec(name, ok, ev):
    results.append((name, ok, ev))
    print("  [%s] %s\n        %s" % ("PASS" if ok else "FAIL", name, ev), flush=True)


def run_bof(aid, args, timeout=45):
    """下发 `execute bof spawn_detached.o <args>`，等该 task 的 107，返回 a_text。"""
    base = max_id(aid)
    e2e.send_raw(aid, "execute bof %s %s" % (BOF, args))
    tid, t0 = None, time.time()
    while tid is None and time.time() - t0 < 30:
        for d in _rows(aid, base):
            if d.get("type") == 106 and "spawn_detached" in str(d.get("a_cmdline", "")):
                tid = d.get("a_task_id")
        if tid is None:
            time.sleep(1)
    t0 = time.time()
    while time.time() - t0 < timeout:
        for d in _rows(aid, base):
            if d.get("type") == 107 and d.get("a_task_id") == tid:
                return str(d.get("a_text", ""))
        time.sleep(1)
    return None


def run_cmd(aid, cmd, timeout=60, needle="uid="):
    """下发普通命令，等 107 回执；返回耗时秒数或 None。"""
    base = max_id(aid)
    e2e.send_raw(aid, cmd)
    t0 = time.time()
    while time.time() - t0 < timeout:
        for d in _rows(aid, base):
            if d.get("type") == 107 and needle in str(d.get("a_text", "")):
                return round(time.time() - t0, 1)
        time.sleep(1)
    return None


def payload_facts(path):
    try:
        txt = open(path).read()
    except OSError:
        return None
    d = {"raw": txt}
    for line in txt.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            d.setdefault(k, v)
    return d


def sid_pgid(p):
    """/proc/<pid>/stat 里 comm 之后依次是 state ppid pgrp session…"""
    try:
        parts = open("/proc/%d/stat" % p).read().rsplit(")", 1)[1].split()
        return int(parts[3]), int(parts[2])
    except Exception:
        return None, None


def main():
    # ── 0. 编译载荷（静态 musl，脱离 agent 后仍能跑）──
    open("/tmp/p01_payload.c", "w").write(PAYLOAD_C)
    r = subprocess.run("musl-gcc -static -O2 -o %s /tmp/p01_payload.c" % PL,
                       shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        print("载荷编译失败:", r.stderr[:300]); sys.exit(2)
    for p in (PL2, PLK):
        subprocess.run("cp -f %s %s" % (PL, p), shell=True)
    print("[p01] 载荷 %d B（静态 musl）" % os.path.getsize(PL), flush=True)
    for f in (OUT1, OUT2):
        try: os.remove(f)
        except OSError: pass

    binp = e2e.gen_agent("TestHTTP", "p01")
    pid = subprocess.Popen([binp], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL,
                           env=dict(os.environ, AX_ROTATE_MS="600000")).pid
    aid = e2e.wait_registered("TestHTTP", pid, timeout=90)
    print("[p01] agent id=%s pid=%d\n" % (aid, pid), flush=True)
    try:
        # ── T1：默认 argv ────────────────────────────────────────────────
        txt = run_bof(aid, PL)
        m = re.search(r"pid=(\d+)", txt or "")
        ok1 = bool(m) and "[+]" in (txt or "") and "verified" in (txt or "")
        rec("A1 回执为成功文案且带 detached pid", ok1, "a_text=%r" % (txt or "")[:170])
        if not ok1:
            return
        npid = int(m.group(1))
        rec("A2 源 ELF 已 unlink（S1）", not os.path.exists(PL),
            "%s exists=%s" % (PL, os.path.exists(PL)))

        f1 = payload_facts(OUT1)
        a_sid, a_pgid = sid_pgid(pid)
        # 判据不是 "PID == SID"：setsid 是**中间子进程**调的，会话首进程是它（很快退出），
        # 孙进程只是继承了那个会话。真正要证的是"载荷不在 agent 的会话/进程组里"，
        # 否则 agent 那边一个 killpg 就能连带把载荷带走。
        rec("A3 载荷在跑、已脱离 agent 的父子/会话/进程组",
            bool(f1) and f1.get("ppid") != str(pid)
            and f1.get("sid") != str(a_sid) and f1.get("pgid") != str(a_pgid),
            "载荷 pid=%s ppid=%s sid=%s pgid=%s / agent pid=%d sid=%s pgid=%s"
            % ((f1 or {}).get("pid"), (f1 or {}).get("ppid"), (f1 or {}).get("sid"),
               (f1 or {}).get("pgid"), pid, a_sid, a_pgid))

        cl = open("/proc/%d/cmdline" % npid, "rb").read().replace(b"\x00", b" ").strip() \
            if os.path.exists("/proc/%d" % npid) else b"<gone>"
        rec("A4 argv[0] 默认伪装 [kworker/0:1]",
            (f1 or {}).get("argv0") == "[kworker/0:1]" and b"[kworker/0:1]" in cl,
            "/proc/%d/cmdline=%r；载荷自报 argv0=%s" % (npid, cl, (f1 or {}).get("argv0")))

        fds = [(k, v) for k, v in (f1 or {}).items() if k.startswith("fd")]
        socks = [x for x in fds if "socket" in x[1]]
        # P1-b 起载体 fd 一律 CLOEXEC（execve("/proc/self/fd/N") 的路径解析先于
        # 内核关闭 CLOEXEC fd）→ execve 成功后**一个继承 fd 都不留**，只剩
        # 0/1/2（都重定向到 /dev/null）。B6：/proc/<载荷pid>/fd 里不该有可执行文件。
        extra = [x for x in fds if x[0] not in ("fd0", "fd1", "fd2")]
        rec("A5 fd 卫生：无 socket 泄漏、继承集仅 stdio",
            len(socks) == 0 and len(extra) == 0,
            "载荷继承 fd: %s" % ", ".join("%s=%s" % (k, v) for k, v in fds))

        # P1-b：载体 = tmpfs 文件（写完立刻 unlink），不是 memfd。"memfd:" 是内核
        # 硬编码前缀，本身就是一级 fileless 执行 IOC —— 随机化后缀躲不掉它，
        # 只能整条路线换掉（方案 §3.4 / 清单 A5·B5·B6）。
        tex = (f1 or {}).get("exe", "")
        traw = (f1 or {}).get("raw", "")
        rec("A6 载体为 tmpfs 而非 memfd（<dir>/.m<8hex> (deleted)）",
            bool(re.match(r"^/dev/shm/\.m[0-9a-f]{8} \(deleted\)$", tex))
            and "memfd:" not in tex and "memfd:" not in traw,
            "exe=%r maps=%r" % (tex, traw[:120]))

        # ── A7：agent 未被拖住（判据两条，都不用猜）────────────────────
        age, alive = tick_age(aid, pid)
        lat = run_cmd(aid, "getuid", 60)
        rec("A7 spawn 后 agent 未被阻塞",
            bool(alive) and (age is not None and age < 60) and lat is not None,
            "tick age=%.1fs；其后 getuid 回执耗时=%ss" % (age if age else -1, lat))

        # ── T2：调用方指定 argv[0] ─────────────────────────────────────
        # 注意：console 明文分词是 strings.Fields —— **引号不是 shell 语义，会字面
        # 传进去**（第一版写了 '[kworker/u8:2]'，载荷收到的 argv0 就带上了引号）。
        txt2 = run_bof(aid, "%s [kworker/u8:2] %s" % (PL2, OUT2))
        f2 = payload_facts(OUT2)
        rec("A8 argv[0] 可控 + 回执成功",
            "[+]" in (txt2 or "") and (f2 or {}).get("argv0") == "[kworker/u8:2]",
            "a_text=%r；载荷自报 argv0=%s" % ((txt2 or "")[:90], (f2 or {}).get("argv0")))
        n2 = re.search(r"\.(m[0-9a-f]{8}) \(deleted\)$", (f2 or {}).get("exe", ""))
        n1 = re.search(r"\.(m[0-9a-f]{8}) \(deleted\)$", tex)
        rec("A6b 两次 spawn 的载体名不同（真随机）",
            bool(n1) and bool(n2) and n1.group(1) != n2.group(1),
            "%s vs %s" % (n1.group(1) if n1 else "?", n2.group(1) if n2 else "?"))

        # ── T3：--keep ────────────────────────────────────────────────
        txt3 = run_bof(aid, "%s --keep" % PLK)
        rec("A9 --keep 保留源文件",
            os.path.exists(PLK) and "source kept" in (txt3 or ""),
            "exists=%s；a_text=%r" % (os.path.exists(PLK), (txt3 or "")[:110]))

        # ── T4：失败路径 ──────────────────────────────────────────────
        txt4 = run_bof(aid, "/tmp/p01_no_such_file_xyz")
        lat4 = run_cmd(aid, "getuid", 60)
        rec("A10 不存在的路径 → 错误回执且 agent 存活",
            "cannot read" in (txt4 or "") and lat4 is not None,
            "a_text=%r；其后 getuid 回执=%ss" % ((txt4 or "")[:110], lat4))
    finally:
        e2e.kill(pid)
        subprocess.run("pkill -f 'p01_payload'", shell=True)
        try: os.remove(PLK)
        except OSError: pass
        f = sum(1 for _, o, _ in results if not o)
        print("\nPASS=%d FAIL=%d" % (len(results) - f, f))
        print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"), flush=True)
        sys.exit(0 if f == 0 else 1)


# 守卫：p11_proc_meta_verify.py 会 `from p01_detached_spawn import run_bof, run_cmd`
# 复用这里的通道。没有守卫时 import 就整套跑完并以 sys.exit() 终止导入方 ——
# p11 自己的 T0–T11 从来没机会执行（首轮实测：日志停在 A10 + RESULT 就没了，见 ISS-036）。
# 回归 p01 请单独 `python3 p01_detached_spawn.py`。
if __name__ == "__main__":
    main()
