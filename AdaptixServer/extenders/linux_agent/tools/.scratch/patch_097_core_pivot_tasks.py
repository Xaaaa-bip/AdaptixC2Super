#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_097_core_pivot_tasks.py — 修 core：gopher 系链路上 pivot 子 agent 的任务发不出去。

用法：
    python3 patch_097_core_pivot_tasks.py apply     # 打补丁（幂等）
    python3 patch_097_core_pivot_tasks.py revert    # 回滚（幂等）
    python3 patch_097_core_pivot_tasks.py status     # 看当前状态

--------------------------------------------------------------------------------
症状（p33 反向 pivot 实测）
--------------------------------------------------------------------------------
反向 pivot 的**链路层已通**：父 agent `listen 42002` 真在听；子 agent 零新增监听
端口、成功注册（teamserver 广播 `新的 TCP 中转 agent`）。但**数据面不通**：
子 agent 的 `getuid` 永远 `a_completed:false`，父侧 console 里**完全没有
PIVOT_EXEC 任务记录** ⇒ 子 agent 的任务从未被路由到父。

--------------------------------------------------------------------------------
根因（两处，必须一起改）
--------------------------------------------------------------------------------
core 里"把任务交给 agent"有两条通道，**只有一条带 pivot**：

  · `TsAgentGetHostedAll`   (core/server/ts_agent.go:213)
        → `TsTaskGetAvailableAll`   (ts_tasks.go)  ✅ 含 extractPivotTasks
  · `TsAgentGetHostedTasks` (core/server/ts_agent.go:250)
        → `TsTaskGetAvailableTasks` (ts_tasks.go)  ❌ 无 extractPivotTasks

谁走哪条（全树 grep 实测）：
  走 All   ：beacon_listener_http / ws / dns / discord / https_emu   ← beacon 系
  走 Tasks ：gopher_listener_tcp（pl_transport.go:445）、agent_kharon
  都不走   ：linux_listener_tcp（只有 TsAgentIsExists/TsAgentCreate 两个方法）

★ 关键否证：**没有任何监听器插件自己补 pivot**。kharon 只在 Teamserver 接口里
  声明了 `TsTasksPivotExists` / `TsTaskGetAvailablePivotAll`，全树 grep 无调用点。
  ⇒ pivot 只在 beacon 系监听器上能用；gopher 系（含本 rig 的父 agent）一律
  **静默丢任务**——不报错、不落日志、console 里什么也看不到。

第二处阻断（**即使补上 pivot 也发不出去**）：
  `TsAgentGetHostedTasks` 的提前返回只看 agent 自己的 HostedTasks / HostedTunnelTasks：
      if tasksCount == 0 && agent.HostedTunnelTasks.Len() == 0 { return []byte(""), nil }
  而 pivot 场景下父 agent 自己**没有任务**，任务全在子 agent 队列里
  ⇒ 函数在走到取任务代码之前就 return 空。两处必须一起改。

--------------------------------------------------------------------------------
修法（**只改这一个函数**，语义对齐 TsAgentGetHostedAll）
--------------------------------------------------------------------------------
  1. 提前返回条件补 tunnelData + pivot（用 TsTasksPivotExists，与 All 版一致）；
  2. 取任务改走 `TsTaskGetAvailableAll`（含 extractPivotTasks）。
  **不改** `TsTaskGetAvailableTasks` 本身：它还挂在 kharon 的 extender 接口上，
  影响面更大；`TsAgentGetHostedTasks` 是 gopher 系唯一的取任务入口，改这里足够。

★ 安全性：不改 C 侧、不改 axs、不改任何 .so 接口
  ⇒ **agent 载荷字节零变化**，只换 core 二进制。
  ⇒ 全树唯一一处 `ts.TsTaskGetAvailableTasks(agentData.Id, maxDataSize)`（grep -c = 1），
     不会误伤 `...TasksCount`（参数不同，字符串不匹配）。

--------------------------------------------------------------------------------
回滚
--------------------------------------------------------------------------------
  BAKDIR=/root/gp5probe/bak15/p33_core
  备份：BAKDIR/ts_agent.go.pre_p97
  `revert` 用备份整文件还原（不是反向替换）⇒ 不受后续手工改动影响。
  只移不删：备份永久保留。
"""

import hashlib
import os
import shutil
import sys

TARGET = "/root/cc/AdaptixServer/core/server/ts_agent.go"
BAKDIR = "/root/gp5probe/bak15/p33_core"
BAKFILE = os.path.join(BAKDIR, "ts_agent.go.pre_p97")

OLD = '''	agentData := agent.GetData()
	tasksCount := agent.HostedTasks.Len()
	if tasksCount == 0 && agent.HostedTunnelTasks.Len() == 0 {
		return []byte(""), nil
	}

	tasks, _, err := ts.TsTaskGetAvailableTasks(agentData.Id, maxDataSize)
	if err != nil {
		return nil, err
	}
'''

NEW = '''	agentData := agent.GetData()
	tasksCount := agent.HostedTasks.Len()
	tunnelConnectCount := agent.HostedTunnelTasks.Len()
	tunnelTasksCount := agent.HostedTunnelData.Len()
	// pivot 子 agent 的任务也走这条通道下发：父 agent 自己往往没有任务，
	// 任务全在子 agent 的队列里 ⇒ 只看自己的队列会在这里提前返回空，
	// 子 agent 的任务就永远发不出去（链路通、数据面静默不通）。
	pivotTasksExists := false
	if agent.PivotChilds.Len() > 0 {
		pivotTasksExists = ts.TsTasksPivotExists(agentData.Id, true)
	}
	if tasksCount == 0 && tunnelConnectCount == 0 && tunnelTasksCount == 0 && !pivotTasksExists {
		return []byte(""), nil
	}

	// 必须用 All 版本：只有它带 extractPivotTasks，Tasks 版本没有 pivot。
	tasks, err := ts.TsTaskGetAvailableAll(agentData.Id, maxDataSize)
	if err != nil {
		return nil, err
	}
'''


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def read():
    with open(TARGET, "r", encoding="utf-8") as f:
        return f.read()


def write(s):
    with open(TARGET, "w", encoding="utf-8") as f:
        f.write(s)


def status():
    s = read()
    has_old = OLD in s
    has_new = NEW in s
    print("  target      = %s" % TARGET)
    print("  md5         = %s" % md5(TARGET))
    print("  OLD 在场    = %s" % has_old)
    print("  NEW 在场    = %s" % has_new)
    if has_old and not has_new:
        print("  => 状态：未打补丁")
    elif has_new and not has_old:
        print("  => 状态：已打补丁")
    else:
        print("  => 状态：★ 异常（新旧同时在场或都不在场）")
    print("  BAK 存在    = %s" % os.path.exists(BAKFILE))
    return 0


def apply():
    s = read()
    if NEW in s:
        print("[=] 已打过补丁，跳过（幂等）")
        return 0
    if s.count(OLD) != 1:
        print("[!] OLD 片段在文件中出现 %d 次（应为 1），拒绝写入" % s.count(OLD))
        return 1
    if not os.path.exists(BAKFILE):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(TARGET, BAKFILE)
        print("[+] 备份 -> %s  md5=%s" % (BAKFILE, md5(BAKFILE)))
    else:
        print("[=] 备份已存在，保留 -> %s" % BAKFILE)
    write(s.replace(OLD, NEW, 1))
    print("[+] 已打补丁  md5 %s -> %s" % (md5(BAKFILE), md5(TARGET)))
    return 0


def revert():
    if not os.path.exists(BAKFILE):
        print("[!] 备份不存在：%s" % BAKFILE)
        return 1
    if md5(TARGET) == md5(BAKFILE):
        print("[=] 已是备份状态，跳过（幂等）")
        return 0
    shutil.copy2(BAKFILE, TARGET)
    print("[+] 已回滚  md5 -> %s" % md5(TARGET))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    sys.exit({"apply": apply, "revert": revert, "status": status}.get(cmd, status)())
