#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_100_pivot_dedup.py — 修 pivot 记录按 PivotId 去重（反向 pivot 数据面第二个根因）。

现象
    patch_097 之后，去程已经通了（实测 rig.log [DBG-EPT] datalen= 51，
    ss -tnpi 父侧 bytes_sent 0->55），回程在线路上也发生了
    （ss -tnpi 子侧 bytes_sent 301->389，+88 = 4B 分帧 + 84B 应答），
    但子 agent 的 getuid 永远不完成 —— 数据在 ts 这一跳被静默丢弃。

根因（三条实测证据串起来的）
    ① `ts.pivots` 是 `safe.Slice`（core/server/server.go:52），`Put` 只 append
       **不去重**；`TsGetPivotInfoById` / `TsGetPivotById` 都 `return` **第一个**
       匹配 ⇒ 同 id 有两条时，永远命中先入队的那条。
    ② pivot id 由子 agent 的 watermark 派生（= 子 agent id 前 4 字节），是
       **确定性的** —— 三次独立实测都是 d651ba3d。所以「teamserver 重启时从 DB
       恢复的陈旧记录」与「同一子机这一轮新建的记录」必然同 id。
       实测：rig 启动 `Restored 1 pivots`(07:25:41)，随后两次
       `[-] pivot d651ba3d already exists`(07:26:49 / 07:33:33)。
    ③ 插件回程分支 `_, _, childAgentId := Ts.TsGetPivotInfoById(pivotId)` +
       `_ = Ts.TsAgentProcessData(childAgentId, params.Data)` —— 命中陈旧记录
       ⇒ 拿到的 ChildAgentId 是个早死掉的 agent ⇒ getAgent 失败 ⇒ **错误被 `_ =`
       吞掉**，84 字节回包静默消失。

    ★ 红/绿对照（同一 rig、同一脚本、同一 DB fixture，唯一变量 = 代码）：
        未打本补丁 ⇒ 判据 10/11，只 N5 红（EXC: '"a_completed":true' not seen）
        打了本补丁 ⇒ 判据 13/13，N5 绿 `getuid -> uid=0(root) euid=0(root)`
                  且 N5 的路由证据：DUP 探测行**消失**，回包投给活着的子机
                      [DBG-PC] NEW pivotId= d651ba3d newChild= 8d707c99 isRestore= false
                      [DBG-PD] agent= 8d707c99 len= 84

修法（三处，都在 core/server/ts_pivot.go）
    A. 新增 `purgePivotById(list *safe.Slice, pivotId)`：删**所有**同 id 条目。
       Delete 是「删除并左移」⇒ 命中时下标**不递增**（原实现是 `break`，
       只删第一条，重复项永久残留 —— TsPivotDelete 里也是同一个写法）。
    B. 新增 `(ts) tsPivotPurge(pivotId, withDb)`：清内存全部同 id 条目（+ 可选清 DB 行）。
       **刻意不碰** agent 的 Mark/PivotParent —— 这是它和 TsPivotDelete 的关键
       区别：子机断线重拨时，旧记录的 ChildAgentId 恰好就是**正在接入的这台子机**，
       走 TsPivotDelete 会把刚接进来的 agent 标成 "Unlink"。
       ★ `withDb` 这个开关是**实测逼出来的**：`TsPivotCreate` 从 DB 恢复
       （isRestore=true）时**不会**走 `DbPivotInsert`，所以如果无条件删 DB 行，
       恢复出来的 pivot 就只在本次开机有效 —— 实测「植入 fixture 行 → 重启 →
       `Restored 1 pivots` → 再查表 === []」，下次开机彻底丢失。
       ⇒ Create 里传 `!isRestore`（恢复时只清内存，而启动时内存本就为空，
       等价于**原行为**）；Delete 里传 `true`（保持上游「删内存 + 删 DB」语义）。
    C. `TsPivotCreate`：在三个 Put **之前**清同 id 旧记录；顺带对父 agent 的
       PivotChilds 去重（那里同样是切片，子机每重拨一次就多一条）。
       顺序很关键：删除逻辑按 PivotId 删第一条匹配，新记录若已入队会被误删。
       `TsPivotDelete` 尾部那段循环也换成 `tsPivotPurge(pivotId, true)`（等价 + 去 break）。

    为什么不用现成的 TsPivotDelete 直接清：见 B。

回滚
    仅 `core/server/ts_pivot.go` 单文件改写；BAKDIR 里的
    `ts_pivot.go.pre_p100` **先于写入**存在，revert = 整文件还原。
    ★ 回滚点必须取「**未打 p100 也未打 p099 调试**」的那一版 —— 本补丁写入前，
      已把 `ts_pivot.go.pre_p99dbg`（md5 6b565a7fb3cbfa3a0d2a305f91514c1b，
      `grep -c purgePivotById` == 0）复制成 `ts_pivot.go.pre_p100`。
      不要用旧的 `pre_p100`（那是 p099 调试态，还原会把调试输出带回交付源码）。

用法
    python3 patch_100_pivot_dedup.py apply|revert|status
"""
import hashlib
import os
import shutil
import sys

ROOT = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak15/p33_core"
REL = "core/server/ts_pivot.go"
BAKNAME = "ts_pivot.go.pre_p100"

# ── A + B：插入两个清理函数（放在 TsPivotCreate 之前）─────────────────────

ANCHOR_CREATE = "func (ts *Teamserver) TsPivotCreate(pivotId string, pAgentId string, chAgentId string, pivotName string, isRestore bool) error {"

HELPERS = '''// purgePivotById 从切片里删掉**所有** PivotId == pivotId 的条目，返回删掉的条数。
//
// ★ Delete 的语义是「删除并把后面的元素左移」，所以命中删除之后**同一位置
// 就是下一条**，下标不能递增。原来这里是 `break`（只删第一条）——
// 同 id 的重复项于是永久残留，而 TsGetPivotInfoById 又只认第一条 ⇒
// 陈旧记录会一直压在新记录前面。
func purgePivotById(list *safe.Slice, pivotId string) int {
	removed := 0
	for i := uint(0); i < list.Len(); {
		valuePivot, ok := list.Get(i)
		if !ok {
			i++
			continue
		}
		pivot, ok := valuePivot.(*adaptix.PivotData)
		if !ok {
			i++
			continue
		}
		if pivot.PivotId == pivotId {
			list.Delete(i)
			removed++
			continue
		}
		i++
	}
	return removed
}

// tsPivotPurge 清除 pivot **记录本身**：内存里的全部同 id 条目，
// withDb 为真时连 DB 行一起清（DbPivotDelete 是 DELETE ... WHERE PivotId = ?，
// 删的就是全部匹配行）。
//
// ★ 它刻意**不碰**任何 agent 的 Mark / PivotParent，这是与 TsPivotDelete 的
// 关键区别。TsPivotCreate 里要清同 id 的旧记录，而旧记录的 ChildAgentId
// 很可能就是**正在接入的这台子机**（子机断线重拨 ⇒ pivot id 必然相同）；
// 走 TsPivotDelete 会把刚接进来的 agent 打成 "Unlink" 断链态。
//
// ★ withDb 开关的必要性：从 DB 恢复（isRestore=true）时**不会**执行
// DbPivotInsert，所以恢复路径上绝不能删 DB 行 —— 否则恢复出来的 pivot
// 只在本次开机有效，下次开机彻底丢失。
func (ts *Teamserver) tsPivotPurge(pivotId string, withDb bool) {
	if n := purgePivotById(ts.pivots, pivotId); n > 0 {
		logs.Debug("", "purge stale pivot record: %s x%d", pivotId, n)
	}
	if !withDb {
		return
	}
	if err := ts.DBMS.DbPivotDelete(pivotId); err != nil {
		logs.Debug("", err.Error())
	}
}

'''

# ── C1：TsPivotCreate 里，三个 Put 之前清同 id 旧记录 ────────────────────

PARENT_PUT_OLD = '''	if parentAgent, err := ts.getAgent(pivotData.ParentAgentId); err == nil {
		parentAgent.PivotChilds.Put(pivotData)
	}'''

PARENT_PUT_NEW = '''	// ★ 同一 PivotId 只允许存在一条记录。pivot id 由子 agent 的 watermark 派生
	//   （= 子 agent id 前 4 字节，确定性），所以「teamserver 重启时从 DB 恢复的
	//   陈旧记录」与「同一子机这一轮新建的记录」**必然同 id**。而 ts.pivots 是
	//   切片、Put 只 append 不去重，TsGetPivotInfoById 又只返回**第一个**匹配
	//   ⇒ 陈旧记录把回程数据路由到早已不存在的 ChildAgentId 上，投递失败还被
	//   调用方 `_ =` 吞掉 —— 现象就是「链路层全绿、数据面静默不通」。
	//
	//   清理必须在新记录入队**之前**：删除逻辑按 PivotId 删第一条匹配，
	//   新记录若已入队会被误删。
	//
	//   withDb 取 !isRestore：恢复路径上 DbPivotInsert 不会执行，删了 DB 行
	//   就再也补不回来（下次开机这条 pivot 直接消失）；而启动时内存本就为空，
	//   所以恢复路径只清内存 == 完全不动 == 原行为。
	ts.tsPivotPurge(pivotId, !isRestore)

	if parentAgent, err := ts.getAgent(pivotData.ParentAgentId); err == nil {
		// 父 agent 的 PivotChilds 同样是切片，子机每重拨一次就 append 一条；
		// 不去重会让 extractPivotTasks / TsTasksPivotExists 为同一条遍历多次。
		purgePivotById(parentAgent.PivotChilds, pivotId)
		parentAgent.PivotChilds.Put(pivotData)
	}'''

# ── C2：TsPivotDelete 尾部换成 tsPivotPurge ─────────────────────────────

DELETE_TAIL_OLD = '''	for i := uint(0); i < ts.pivots.Len(); i++ {
		valuePivot, ok := ts.pivots.Get(i)
		if ok {
			if valuePivot.(*adaptix.PivotData).PivotId == pivotId {
				ts.pivots.Delete(i)
				break
			}
		}
	}

	err := ts.DBMS.DbPivotDelete(pivotId)
	if err != nil {
		logs.Error("", err.Error())
	}'''

DELETE_TAIL_NEW = '''	// 删**所有**同 id 条目并同步 DB 行（原来这里 `break` 只删第一条，
	// 同 id 的重复项会永久残留；DbPivotDelete 本身删的就是全部匹配行，
	// 两边语义这才对齐）。这里 withDb=true，保持上游「删内存 + 删 DB」语义。
	ts.tsPivotPurge(pivotId, true)'''

# ── D：补 safe 包导入 ───────────────────────────────────────────────────

IMPORT_OLD = '''import (
	"AdaptixServer/core/eventing"
	"AdaptixServer/core/utils/logs"
	"fmt"

	"github.com/Adaptix-Framework/axc2"
)'''

IMPORT_NEW = '''import (
	"AdaptixServer/core/eventing"
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/safe"
	"fmt"

	"github.com/Adaptix-Framework/axc2"
)'''

EDITS = [
    (IMPORT_OLD, IMPORT_NEW),
    (ANCHOR_CREATE, HELPERS + ANCHOR_CREATE),
    (PARENT_PUT_OLD, PARENT_PUT_NEW),
    (DELETE_TAIL_OLD, DELETE_TAIL_NEW),
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
    print("  %s  purgePivotById=%s  tsPivotPurge=%s  md5=%s" %
          (REL, "func purgePivotById" in s, "func (ts *Teamserver) tsPivotPurge" in s, md5(path())))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    sys.exit({"apply": apply, "revert": revert, "status": status}.get(cmd, status)())
