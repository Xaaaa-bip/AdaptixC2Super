#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_101_silent_drop_log.py — 消除 agent 数据包的三个静默丢弃点（纯日志）。

动机（这次排查真正的代价所在）
    反向 pivot 的根因是「pivot 记录错配 ⇒ 回程被投给已死的子 agent」。
    但这个错误**在日志里一个字都没有**：
      ① core 侧 `TsAgentProcessData` 的 `getAgent` 失败 → `return err`，不打日志；
      ② extender 侧 relay 分支是 `_ = Ts.TsAgentProcessData(childAgentId, params.Data)`
         —— 错误被 `_ =` 吞掉，连返回值都没人看；
      ③ 就算 ①②都放行，`Agent.ProcessData` 里解密失败也只 `return err`，不打日志。
    三者叠加 = 现象只有「链路层全绿、数据面静默不通」，定位全靠 `ss -tnpi`
    字节计数反推。**静默失败本身就是缺陷**，这条补丁让它可见。

修法（只改 `TsAgentProcessData` 一个函数，纯日志，成功路径零变化）
    A. getAgent 失败 → `logs.Error`，带上 agentId 和包长。
       ★ 这条是关键：它正是「数据被投给不存在的 agent」的唯一直接证据。
    B. `agent.ProcessData` 失败 → `logs.Error`（区分「会话密钥不匹配」和「协议错位」
       要靠 agentId + 包长，所以一并打出来）。
    C. 包长 <= 4 按协议是 no-op（不解析），但「包到了却什么也没发生」也难排查
       → `logs.Debug` 留痕。用 Debug 是因为它是合法路径，不该刷 Error。

    为什么不去改 extender 侧的 `_ =`：那要重建 `agent_linux.so`（交付物字节会变，
    需重做双构建确定性验证），而 core 侧这三条日志**已经覆盖了同样的信息**，
    收益相同、代价低一个数量级。

回滚
    纯 `core/server/ts_agent.go` 单文件改写；BAKDIR 里的
    `ts_agent.go.pre_p101` **先于写入**存在，revert = 整文件还原。
    ★ 本补丁的回滚点是「097 已打、098/099 已摘」的状态，不要与 p097/p098 的
      回滚点混淆（那两处的还原会把 p097 一起抹掉）。

用法
    python3 patch_101_silent_drop_log.py apply|revert|status
"""
import hashlib
import os
import shutil
import sys

ROOT = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak15/p33_core"
REL = "core/server/ts_agent.go"
BAKNAME = "ts_agent.go.pre_p101"

HEAD_OLD = '''func (ts *Teamserver) TsAgentProcessData(agentId string, bodyData []byte) error {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		return err
	}
'''

HEAD_NEW = '''func (ts *Teamserver) TsAgentProcessData(agentId string, bodyData []byte) error {
	agent, err := ts.getAgent(agentId)
	if err != nil {
		// ★ 不要静默：走到这里说明上游把一个 agent 的数据包路由给了**不存在的
		//   agent**（典型：pivot 记录过期/错配 ⇒ 回程数据投给已死的子 agent）。
		//   调用方（extender 的 relay 分支）是 `_ = TsAgentProcessData(...)`，
		//   返回值没人看 —— 不打日志的话，现象就只剩「链路全绿、数据面静默不通」。
		logs.Error("", "process data: agent %s not found: %v (len=%d)", agentId, err, len(bodyData))
		return err
	}
'''

TAIL_OLD = '''	if len(bodyData) > 4 {
		return agent.ProcessData(bodyData)
	}

	return nil
}
'''

TAIL_NEW = '''	if len(bodyData) > 4 {
		if err := agent.ProcessData(bodyData); err != nil {
			// 解密/解析失败同样是黑洞：agentId + 包长是区分
			// 「会话密钥不匹配」与「协议错位」的最小充分信息。
			logs.Error("", "process data: agent %s: %v (len=%d)", agentId, err, len(bodyData))
			return err
		}
		return nil
	}

	// 长度 <= 4 的包按协议是 no-op（不解析）。合法路径，用 Debug 留痕即可，
	// 否则「包到了却什么也没发生」同样无从查起。
	logs.Debug("", "process data: agent %s: drop short packet len=%d", agentId, len(bodyData))
	return nil
}
'''

EDITS = [
    (HEAD_OLD, HEAD_NEW),
    (TAIL_OLD, TAIL_NEW),
]


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def path():
    return os.path.join(ROOT, REL)


def apply():
    bak = os.path.join(BAKDIR, BAKNAME)
    if not os.path.exists(bak):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(path(), bak)
        print("  [bak] %s -> %s" % (REL, bak))
    else:
        print("  [bak] 已存在，未覆盖 %s" % bak)

    s = open(path(), encoding="utf-8").read()
    for old, new in EDITS:
        if new in s and old not in s:
            print("  [skip] 已应用（片段已在）")
            continue
        n = s.count(old)
        if n != 1:
            print("  [REFUSE] 片段匹配 %d 次（应为 1），拒绝改写：%r" % (n, old[:60]))
            return 1
        s = s.replace(old, new)
    open(path(), "w", encoding="utf-8").write(s)
    print("  [ok] %s 已打补丁（%d 处）  md5=%s" % (REL, len(EDITS), md5(path())))
    return 0


def revert():
    bak = os.path.join(BAKDIR, BAKNAME)
    if not os.path.exists(bak):
        print("  [ERR] 缺回滚点 %s" % bak)
        return 1
    shutil.copy2(bak, path())
    print("  [revert] %s <- %s  md5=%s" % (REL, bak, md5(path())))
    return 0


def status():
    s = open(path(), encoding="utf-8").read()
    print("  %s  log_getAgent=%s  log_processData=%s  md5=%s" %
          (REL, "not found: %v (len=%d)" in s, "drop short packet len=%d" in s, md5(path())))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    sys.exit({"apply": apply, "revert": revert, "status": status}.get(cmd, status)())
