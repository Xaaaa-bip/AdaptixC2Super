#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_099_dbg_relay.py — 临时调试：把「回程（子→父→ts）」这一跳打成可见。

背景（为什么需要这条）
    反向 pivot 的**去程**已经用 patch_097 修好并实测：
      rig.log: [DBG-EPT] child= 8e581249 pivotId= d651ba3d datalen= 51
      ss -tnpi: 父(42002) bytes_sent 0 -> 55 = 4B 分帧 + 51B 子 agent 任务包
    而且**回程也在线路上发生**了：
      ss -tnpi: 子(55436) bytes_sent 301 -> 389（+88 = 4B 分帧 + 84B 应答）
    但子 agent 的 getuid 永远不完成 ⇒ 卡在 ts 把 84 字节交给子 agent 的那一跳。

假设（本补丁要证伪/证实的）
    `ts.pivots` 是 `safe.Slice`（server.go:52），`Put` = append，**不去重**。
    rig 启动日志 `Restored 1 pivots`（07:25:41）把 DB 里那条陈旧 pivot 恢复进内存；
    新 pivot 的 id 是**确定性的**（三次独立实测都是 d651ba3d）⇒ 撞车，
    DB 插入报 `pivot d651ba3d already exists`（07:26:49 / 07:33:33 两次实测）。
    `TsGetPivotInfoById` 遍历切片返回**第一个**匹配 ⇒ 命中的是**陈旧那条**，
    其 ChildAgentId 是个早死掉的 agent ⇒ `TsAgentProcessData` 里 `getAgent` 失败，
    而插件侧是 `_ = Ts.TsAgentProcessData(...)`，**错误被吞**，回包静默消失。

    这三条 println 定点回答三个问题：
      [DBG-PC]  TsPivotCreate 时是否已存在同 id 的旧 pivot？旧的是谁？
      [DBG-GPI] 回程查找命中的是哪一条（parent/child）？还是 MISS？
      [DBG-PD]  拿到的 childAgentId 能不能 getAgent 成功？len 对不对？

护栏
    纯 println，无逻辑改动；REVERT = 从 BAKDIR 整文件还原（回滚点先于写入存在）。
    APPLY 之后必须重建 core，且**只装进 rig**，交付树/运行态一概不碰。
    验完立刻 revert，否则交付源码会带调试输出。

用法
    python3 patch_099_dbg_relay.py apply|revert|status
"""
import hashlib
import os
import shutil
import sys

ROOT = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak15/p33_core"

FILES = {
    "core/server/ts_pivot.go": "ts_pivot.go.pre_p99dbg",
    "core/server/ts_agent.go": "ts_agent.go.pre_p99dbg",
}

# ── ts_pivot.go：① TsGetPivotInfoById 命中/未命中 ② TsPivotCreate 撞车探测 ──

PIVOT_GPI_OLD = '''func (ts *Teamserver) TsGetPivotInfoById(pivotId string) (string, string, string) {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotId == pivotId {
			return pivot.PivotName, pivot.ParentAgentId, pivot.ChildAgentId
		}
	}
	return "", "", ""
}'''

PIVOT_GPI_NEW = '''func (ts *Teamserver) TsGetPivotInfoById(pivotId string) (string, string, string) {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotId == pivotId {
			println("[DBG-GPI] HIT", pivotId, "name=", pivot.PivotName, "parent=", pivot.ParentAgentId, "child=", pivot.ChildAgentId)
			return pivot.PivotName, pivot.ParentAgentId, pivot.ChildAgentId
		}
	}
	println("[DBG-GPI] MISS", pivotId)
	return "", "", ""
}'''

PIVOT_PUT_OLD = '''	ts.pivots.Put(pivotData)

	if !isRestore {'''

PIVOT_PUT_NEW = '''	if oldPivot := ts.TsGetPivotById(pivotId); oldPivot != nil {
		println("[DBG-PC] DUP pivotId=", pivotId, "oldName=", oldPivot.PivotName, "oldParent=", oldPivot.ParentAgentId, "oldChild=", oldPivot.ChildAgentId, "newChild=", chAgentId)
	} else {
		println("[DBG-PC] NEW pivotId=", pivotId, "newChild=", chAgentId, "isRestore=", isRestore)
	}
	ts.pivots.Put(pivotData)

	if !isRestore {'''

# ── ts_agent.go：TsAgentProcessData 入口 + getAgent 失败 ──

PD_OLD = '''func (ts *Teamserver) TsAgentProcessData(agentId string, bodyData []byte) error {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		return err
	}'''

PD_NEW = '''func (ts *Teamserver) TsAgentProcessData(agentId string, bodyData []byte) error {
	println("[DBG-PD] agent=", agentId, "len=", len(bodyData))
	agent, err := ts.getAgent(agentId)
	if err != nil {
		println("[DBG-PD] ERR", err.Error())
		return err
	}'''

EDITS = [
    ("core/server/ts_pivot.go", PIVOT_GPI_OLD, PIVOT_GPI_NEW),
    ("core/server/ts_pivot.go", PIVOT_PUT_OLD, PIVOT_PUT_NEW),
    ("core/server/ts_agent.go", PD_OLD, PD_NEW),
]


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def backup(rel):
    src = os.path.join(ROOT, rel)
    bak = os.path.join(BAKDIR, FILES[rel])
    if not os.path.exists(bak):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(src, bak)
        print("  [bak] %s -> %s" % (rel, bak))
    else:
        print("  [bak] 已存在，未覆盖 %s" % bak)


def apply():
    for rel, old, new in EDITS:
        p = os.path.join(ROOT, rel)
        backup(rel)
        s = open(p, encoding="utf-8").read()
        if new in s and old not in s:
            print("  [skip] %s 已应用" % rel)
            continue
        n = s.count(old)
        if n != 1:
            print("  [REFUSE] %s 匹配 %d 次（应为 1），拒绝改写" % (rel, n))
            return 1
        open(p, "w", encoding="utf-8").write(s.replace(old, new))
        print("  [ok] %s 已打补丁（%d 处）" % (rel, 1))
    for rel in FILES:
        print("  md5 %s  %s" % (md5(os.path.join(ROOT, rel)), rel))
    return 0


def revert():
    rc = 0
    for rel, bakname in FILES.items():
        bak = os.path.join(BAKDIR, bakname)
        if not os.path.exists(bak):
            print("  [ERR] 缺回滚点 %s" % bak)
            rc = 1
            continue
        shutil.copy2(bak, os.path.join(ROOT, rel))
        print("  [revert] %s <- %s  md5=%s" % (rel, bak, md5(os.path.join(ROOT, rel))))
    return rc


def status():
    for rel in FILES:
        p = os.path.join(ROOT, rel)
        s = open(p, encoding="utf-8").read()
        print("  %s  DBG=%s  md5=%s" % (rel, "[DBG-" in s, md5(p)))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    sys.exit({"apply": apply, "revert": revert, "status": status}.get(cmd, status)())
