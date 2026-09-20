#!/usr/bin/env python3
"""P9-9.3 / B7 验收：agent 的 `/proc/<pid>/environ` 不携带操作者环境。

**被测对象**：agent 进程的 `/proc/<pid>/environ` —— 即初始栈上的 env 区块
（`mm->env_start..env_end`）。内核在 exec 时拷进去，**进程自己不清就永远在**，
同为 root 时随时可读 —— 进程画像 / 归因情报的第一梯队来源。

── 代码真相（源码定位，修前实测与它吻合）────────────────────────────────
    `main.c:1031 opsec_self_cover(g_argv)`      在**任何网络活动之前**
  → `opsec.c:545  opsec_self_carrier(argv)`     `#ifndef BUILD_SO`，成功不返回
  → `opsec.c:491  AxExecFdArgv(cfd, argv, (char **)0)`   ← **envp = NULL**
  → `bof_api.c:915 sys_execve("/proc/self/fd/N", argv, envp)`
  ⇒ 映像被从 tmpfs 载体**整个换掉**，新映像的 env 区 = 空。
  这是 `opsec.c:489-490` 明写的设计选择：零 libc 构建没有 environ 符号 +
  真内核线程本来就没有用户态环境变量（顺带少一处指纹）。

  ⇒ **plan 里 9.3 标的「B7 零清理」是纸面缺口**：该属性由载体化顺手关掉，
    不是缺。本判据把它**钉住**，防的是将来有人为了 `~`/`env` 能用来
    「把 envp 继承回来」，那会静默重开这条泄漏。

★★ 判别力前提（三条，缺一条本判据作废 —— ISS-083 的教训）：
  ① **仪器**：同一次运行、**同一个 env dict** 起的对照进程（`/bin/sleep`）必须
     **读得到**标记。否则「读不到标记」可以廉价地由「读法失灵」满足。
  ② **机制**：agent 的 `/proc/<pid>/exe` 必须**已不在原落地路径**上（= 载体化
     确实发生）。这一条把「因」和「果」绑在一起 —— 没有它，「env 为空」可能
     来自别的原因（比如 launch 方式就不带 env）。
  ③ **输入非空**：env dict 是**显式构造**后交给 `Popen(env=...)`，**不是**
     `os.environ[k]=v`。★ 上一段踩过这个坑：`os.environ[k]=v` 走 `putenv`，
     改的是 libc 的 `environ`，**不改初始栈上的 env 区**（而 `/proc/<pid>/environ`
     读的正是后者）⇒ 自测看不到标记，却会被「读法失灵」冒充成结论。显式 env
     dict 经 `Popen(env=)` 交给 execve ⇒ **必然**落在子进程 env 区（由 ① 证明）。

★ 字节长度 = 0 的含义（**别读反**）：长度是 `env_end - env_start`。
  「换成空 env 重新 exec」⇒ 长度塌成 0；「就地清零」⇒ 长度不变、内容变空。
  本处实测是**前者**，与上面的机制一致 —— 所以长度 0 在这里是**果的佐证**，
  不是「漏没漏」的判据；机制锚点是 ②。

★ 已知有界缺口（据实留档，不在本判据内）：
  · 载体化**任何一步失败**（readlink / 读自身 / tmpfs 不可用）⇒ 原映像继续
    ⇒ env 区**保留台面环境**。判据只在「迁移成功」这条主路径上成立。
  · `BUILD_SO` 构建（ld.so.preload 注入）不迁移 —— 但那时的 environ 是**宿主
    进程的**，动它反而毁宿主，本来就不该动。
  · `Popen` 到迁移完成之间有毫秒级窗口，env 还是继承来的（只有已经在盯
    `/proc` 的对手抓得到）。
  · 空 env 让 5 处读者退化：`bof_api.c:601 AxGetEnv` 干净返回 -1、
    `tasks_fs.c:124 normalize_path` 的 `~` **静默落到 `/tmp`**（:150 的兜底，
    不报错 —— 会让操作者 ls 错目录）、`tasks_linux.c:122` 的 `env` 命令、
    `timing.c:119`。这是**功能**缺口，不是隐匿缺口，方向与 B7 相反。

★ 无 BPF / 无 rootkit ⇒ 无串行要求，批次里放哪都行。整跑约 1 分钟。
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e
import p15_redact_verify as p15            # getuid_ok / kill

LISTENER = "TestHTTP"
MARK = "AXP30MARKER"
# ① 标记（判别力用）② 操作者 SSH 会话串（**归因情报** —— 本判据真正要守的东西）
PLANT = {
    MARK: "LEAKCANARY-7f3a",
    "SSH_CONNECTION": "203.0.113.9 51000 198.51.100.7 22",
}
CARRIER_PFX = ("/dev/shm/.m", "/run/shm/.m")   # = opsec.c:454 的判据前缀对（仅报告用）
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".scratch", "p30_agent.log")
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def environ_of(pid):
    """读得到返回 bytes（空 env 区 = b''），读不到返回 None —— **两者必须分开**。"""
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            return f.read(65536)
    except OSError:
        return None


def exe_of(pid):
    try:
        return os.readlink("/proc/%d/exe" % pid)
    except OSError as e:
        return "<%s>" % e


def launch_with_env(bin_path, env):
    """= `e2e.launch` 的形状（stdout/stderr 收进日志）**加上显式 env**。
    ★ 不改 `e2e.py`：台面归台面，且它被批次里别的脚本共用。"""
    log = open(LOG, "wb")
    try:
        return subprocess.Popen([bin_path], stdout=log, stderr=log, env=env).pid
    finally:
        log.close()


def main():
    env = dict(os.environ)
    env.update(PLANT)                      # ★ 前提③：显式构造，不走 putenv
    pid0, ctrl = 0, None
    try:
        bin0 = e2e.gen_agent(LISTENER, "p30a0")

        # ── 对照与 agent 用**同一个 env dict** 起 ⇒ 唯一变量 = 二进制本身 ──────
        ctrl = subprocess.Popen(["/bin/sleep", "30"], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pid0 = launch_with_env(bin0, env)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] aid=%s  agent pid=%d  对照 pid=%d" % (a0, pid0, ctrl.pid), flush=True)
        time.sleep(1)

        # ── ① 仪器校验：已知阳性样本上证明读法有效 ───────────────────────────
        craw = environ_of(ctrl.pid)
        hit_c = [k for k in PLANT if craw is not None and k.encode() in craw]
        rec("① 仪器：同一 env dict 起的对照进程读得到植入变量",
            len(hit_c) == len(PLANT),
            "对照 environ = %s，命中 %d/%d" %
            ("None(读不到!)" if craw is None else "%d 字节" % len(craw),
             len(hit_c), len(PLANT)))

        # ── ② 机制前提：载体化确实发生（把因和果绑一起）─────────────────────
        exe = exe_of(pid0)
        rec("② ★判别力前提：exe 已不在原落地路径上（⇒ 载体化确实发生）",
            exe != bin0 and os.path.basename(bin0) not in exe,
            "exe = %s" % exe)

        # ── ③ 被测值 ───────────────────────────────────────────────────────
        raw = environ_of(pid0)
        leaked = [k for k in PLANT if raw is not None and k.encode() in raw]
        rec("③ ★★ B7 收官：agent 的 environ **读不到**任何植入变量",
            raw is not None and not leaked,
            "植入 %s ⇒ 命中 %s" % (",".join(sorted(PLANT)), leaked or "无"))
        rec("③b env 区长度塌成 0（= 映像被换掉、新映像 envp=NULL 的果）",
            raw == b"",
            "读回 %s" % ("None(读不到 ⇒ 台面不成立)" if raw is None
                         else "%d 字节（对照 %s）" % (len(raw), len(craw or b""))))

        # ── 报告项（**不是判据**）：exe 落到哪个载体 ─────────────────────────
        #    载体是 tmpfs 还是 memfd 不影响不变量（envp=NULL 两条路都空），
        #    所以前缀只印不断言 —— 否则重构改名会假红。
        print("  [----] 报告：exe 落在 tmpfs 载体前缀 = %s   exe=%s"
              % (any(exe.startswith(p) for p in CARRIER_PFX), exe), flush=True)

        rec("④ 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))
        return 0 if all(R) else 1
    finally:
        try:
            if ctrl is not None:
                ctrl.kill()
                ctrl.wait(timeout=5)
        except Exception as e:
            print("[warn] 收对照: %s" % e, flush=True)
        p15.kill(pid0)
        print("[env] agent 日志 = %s" % LOG, flush=True)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)
        print("RESULT: %s" % ("PASS" if R and all(R) else "FAIL"), flush=True)


if __name__ == "__main__":
    sys.exit(main())
