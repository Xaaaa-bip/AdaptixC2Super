#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_098_dbg_pivot.py — ★ 临时调试补丁（诊断完必须 revert）。

目的：反向 pivot 数据面"任务发不出去"的最后一个未知量——
      `TsAgentGetHostedTasks` 是否真的走到取任务那一步、`PivotChilds` 是否非空。

用法：
    python3 patch_098_dbg_pivot.py apply | revert | status

打在 core/server/ts_agent.go + core/server/ts_tasks.go，只用内置 `println`
（写 stderr ⇒ 进 rig.log），不引入 import。

★ 这不是交付代码：诊断完 `revert`，然后重新构建。revert 是整文件还原，
  不受后续手工改动影响。
BAKDIR=/root/gp5probe/bak15/p33_core/
"""

import hashlib
import os
import shutil
import sys

BAKDIR = "/root/gp5probe/bak15/p33_core"
F_AGENT = "/root/cc/AdaptixServer/core/server/ts_agent.go"
F_TASKS = "/root/cc/AdaptixServer/core/server/ts_tasks.go"
BAK_AGENT = os.path.join(BAKDIR, "ts_agent.go.pre_p98dbg")
BAK_TASKS = os.path.join(BAKDIR, "ts_tasks.go.pre_p98dbg")

# ── 1) TsAgentGetHostedTasks：入口打印决策量 ──────────────────────────────
A_OLD = '''	pivotTasksExists := false
	if agent.PivotChilds.Len() > 0 {
		pivotTasksExists = ts.TsTasksPivotExists(agentData.Id, true)
	}
	if tasksCount == 0 && tunnelConnectCount == 0 && tunnelTasksCount == 0 && !pivotTasksExists {
		return []byte(""), nil
	}
'''
A_NEW = '''	pivotTasksExists := false
	if agent.PivotChilds.Len() > 0 {
		pivotTasksExists = ts.TsTasksPivotExists(agentData.Id, true)
	}
	println("[DBG-HGT]", agentId, "own=", tasksCount, "tc=", tunnelConnectCount, "td=", tunnelTasksCount, "childs=", agent.PivotChilds.Len(), "pivotExists=", pivotTasksExists)
	if tasksCount == 0 && tunnelConnectCount == 0 && tunnelTasksCount == 0 && !pivotTasksExists {
		return []byte(""), nil
	}
'''

# ── 2) extractPivotTasks：每个子 agent 的实际取数结果 ─────────────────────
B_OLD = '''		data, err := ts.TsAgentGetHostedAll(pivotData.ChildAgentId, lostSize)
		if err != nil {
			continue
		}
		pivotTaskData, err := agent.PivotPackData(pivotData.PivotId, data)
		if err != nil {
			continue
		}
'''
B_NEW = '''		data, err := ts.TsAgentGetHostedAll(pivotData.ChildAgentId, lostSize)
		if err != nil {
			println("[DBG-EPT] hostedAll ERR child=", pivotData.ChildAgentId, err.Error())
			continue
		}
		println("[DBG-EPT] child=", pivotData.ChildAgentId, "pivotId=", pivotData.PivotId, "datalen=", len(data))
		pivotTaskData, err := agent.PivotPackData(pivotData.PivotId, data)
		if err != nil {
			println("[DBG-EPT] pack ERR ", err.Error())
			continue
		}
'''

# ── 3) extractPivotTasks 入口：iterating 之前 ────────────────────────────
C_OLD = '''	usedSize = startSize
	for i := uint(0); i < agent.PivotChilds.Len(); i++ {
		value, ok := agent.PivotChilds.Get(i)
'''
C_NEW = '''	usedSize = startSize
	println("[DBG-EPT-IN] childs=", agent.PivotChilds.Len(), "avail=", availableSize, "start=", startSize)
	for i := uint(0); i < agent.PivotChilds.Len(); i++ {
		value, ok := agent.PivotChilds.Get(i)
'''

EDITS = [(F_AGENT, A_OLD, A_NEW, BAK_AGENT),
         (F_TASKS, B_OLD, B_NEW, BAK_TASKS),
         (F_TASKS, C_OLD, C_NEW, BAK_TASKS)]


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def status():
    for path, old, new, _ in EDITS:
        s = open(path, encoding="utf-8").read()
        print("  %s  DBG在场=%s 都无=%s" % (path, new in s, (old not in s and new not in s)))
    return 0


def apply():
    os.makedirs(BAKDIR, exist_ok=True)
    for path, old, new, bak in EDITS:
        s = open(path, encoding="utf-8").read()
        if new in s:
            print("[=] 已打：%s（跳过该片段）" % path)
            continue
        if s.count(old) != 1:
            print("[!] OLD 出现 %d 次（应为 1）：%s" % (s.count(old), path))
            return 1
        if not os.path.exists(bak):
            shutil.copy2(path, bak)
            print("[+] 备份 -> %s" % bak)
        open(path, "w", encoding="utf-8").write(s.replace(old, new, 1))
        print("[+] 已打：%s  md5=%s" % (path, md5(path)))
    return 0


def revert():
    for path, old, new, bak in EDITS:
        if not os.path.exists(bak):
            print("[!] 备份不存在：%s" % bak)
            return 1
        shutil.copy2(bak, path)
        print("[+] 已还原：%s  md5=%s" % (path, md5(path)))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    sys.exit({"apply": apply, "revert": revert, "status": status}.get(cmd, status)())
