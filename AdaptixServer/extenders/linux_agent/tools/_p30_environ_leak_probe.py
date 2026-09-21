#!/usr/bin/env python3
"""P9-9.3 / B7 修前实测（一次性诊断，不进回归批次）。

**被测对象**：agent 进程的 `/proc/<pid>/environ`。

它是**初始栈上的 env 区块**（`mm->env_start..env_end`），内核在 `exec` 时拷进去，
**进程自己不清就永远在**，且同 uid（这里我们就是 root）随时可读 —— rootkit 检测
里这是第一梯队的进程画像来源。而 plan 里 9.3 一支标的是 **B7 零清理**：全仓
`grep environ` 的结果是 agent **只读不写**（5 处读者：`bof_api.c` AxGetEnv、
`tasks_fs.c` 展开 `~`、`tasks_linux.c` 取 HOME / `env` 命令、`timing.c` 测试旋钮），
**没有任何一处清理**。

★ 泄漏路径已经从代码上定位：`e2e.launch()` 是 `Popen([bin_path])` **不带 `env=`**
  ⇒ 子进程**继承台面的整个环境**。真实场景同形 —— 操作者从 shell 起 agent、
  或服务器 `execute` 一条命令，agent 就把**那个会话的全部环境变量**永久背在身上。

★★ 本探针要回答三个问题（先测再改，不猜）：
   ① 修前到底漏什么？—— 把整个 env 区**逐条**打出来，当物证；
   ② 漏到**哪些进程**？—— 全 `/proc` 扫标记，量出传播半径（agent 自己？它的子进程？）；
   ③ 判别力的锚点在哪？—— `/proc/<pid>/environ` 的**字节长度**是
      `env_end - env_start`，只有"就地清零"能保持它；"换个空 env 重新 exec"
      会让长度塌成 0 ⇒ 长度是区分这两种修法的**唯一**证据。

★ 阳性对照（缺了它整条结论作废）：`HOME=` 必须**在**，且标记在修前必须**在** ——
  否则"读不到标记"可以廉价地由"台面把标记弄丢了 / env 区根本读不到"满足。
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/root/cc/AdaptixServer/extenders/linux_agent/tools")
import e2e
import p15_redact_verify as p15

LISTENER = "TestHTTP"
MARK = "AXP30MARKER"
# 四类真实来源：① 本探针的标记 ②③ 操作者 SSH 会话（**归因情报**，最值钱的那类）
# ④ 我们自己命名族（若操作者用它传过参数）
PLANT = {
    MARK: "LEAKCANARY-7f3a",
    "SSH_CONNECTION": "203.0.113.9 51000 198.51.100.7 22",
    "SSH_CLIENT": "203.0.113.9 51000 22",
    "AX_AGENT_TAG": "p30leak",
}


def read_env(pid, cap=65536):
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            return f.read(cap)
    except OSError:
        return None


def show(raw, tag):
    ents = [e.decode("utf-8", "replace") for e in raw.split(b"\x00") if e]
    print("  [%s] 字节长度=%d  条目=%d" % (tag, len(raw), len(ents)), flush=True)
    for e in ents:
        print("       %s" % e[:118], flush=True)
    return ents


def main():
    for k, v in PLANT.items():
        os.environ[k] = v
    print("[env] 已植入 %d 个变量：%s" % (len(PLANT), ", ".join(sorted(PLANT))),
          flush=True)

    pid0 = 0
    try:
        bin0 = e2e.gen_agent(LISTENER, "p30a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] aid=%s pid=%d" % (a0, pid0), flush=True)
        time.sleep(1)

        raw = read_env(pid0)
        if raw is None:
            print("[abort] 连 `/proc/%d/environ` 都读不到 ⇒ 台面不成立" % pid0,
                  flush=True)
            return 2
        ents = show(raw, "agent 自身")
        print("  [阳性对照] HOME= 在吗: %s" % any(e.startswith("HOME=") for e in ents),
              flush=True)
        print("  [被测值　] 标记 %s 在吗: %s" % (MARK, MARK in raw.decode("utf-8", "replace")),
              flush=True)
        print("  [被测值　] 操作者归因变量 SSH_CLIENT 在吗: %s"
              % any(e.startswith("SSH_CLIENT=") for e in ents), flush=True)

        # ── 传播半径：全 /proc 扫标记（谁继承了它）──────────────────────────
        print("\n[scan] 全 /proc 里环境含标记的进程（传播半径）：", flush=True)
        hits = 0
        for d in sorted(os.listdir("/proc")):
            if not d.isdigit():
                continue
            p = int(d)
            b = read_env(p, 8192)
            if b and MARK.encode() in b:
                try:
                    with open("/proc/%d/comm" % p) as f:
                        comm = f.read().strip()
                except OSError:
                    comm = "?"
                mark = " ← agent 自身" if p == pid0 else ""
                print("       pid=%-7d comm=%-20s%s" % (p, comm, mark), flush=True)
                hits += 1
        print("       共 %d 个进程背着我植入的标记" % hits, flush=True)
        return 0
    finally:
        p15.kill(pid0)
        print("\n[env] 现场已清（agent 已杀）", flush=True)


if __name__ == "__main__":
    sys.exit(main())
