package server

import (
	"AdaptixServer/core/eventing"
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/safe"
	"fmt"

	"github.com/Adaptix-Framework/axc2"
)

func (ts *Teamserver) TsGetPivotInfoByName(pivotName string) (string, string, string) {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotName == pivotName {
			return pivot.PivotId, pivot.ParentAgentId, pivot.ChildAgentId
		}
	}
	return "", "", ""
}

func (ts *Teamserver) TsGetPivotInfoById(pivotId string) (string, string, string) {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotId == pivotId {
			return pivot.PivotName, pivot.ParentAgentId, pivot.ChildAgentId
		}
	}
	return "", "", ""
}

func (ts *Teamserver) TsGetPivotByName(pivotName string) *adaptix.PivotData {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotName == pivotName {
			return pivot
		}
	}
	return nil
}

func (ts *Teamserver) TsGetPivotById(pivotId string) *adaptix.PivotData {
	for value := range ts.pivots.Iterator() {
		pivot := value.Item.(*adaptix.PivotData)
		if pivot.PivotId == pivotId {
			return pivot
		}
	}
	return nil
}

// purgePivotById 从切片里删掉**所有** PivotId == pivotId 的条目，返回删掉的条数。
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

func (ts *Teamserver) TsPivotCreate(pivotId string, pAgentId string, chAgentId string, pivotName string, isRestore bool) error {
	// --- PRE HOOK ---
	preEvent := &eventing.EventDataPivotCreate{
		PivotId:       pivotId,
		ParentAgentId: pAgentId,
		ChildAgentId:  chAgentId,
		PivotName:     pivotName,
	}
	if !ts.EventManager.Emit(eventing.EventPivotCreate, eventing.HookPre, preEvent) {
		if preEvent.Error != nil {
			return preEvent.Error
		}
		return fmt.Errorf("operation cancelled by hook")
	}
	// ----------------

	pivotData := &adaptix.PivotData{
		PivotId:       pivotId,
		PivotName:     pivotName,
		ParentAgentId: pAgentId,
		ChildAgentId:  chAgentId,
	}

	if pivotData.PivotName == "" || ts.TsGetPivotByName(pivotData.PivotName) != nil {
		usedNames := make(map[string]bool)
		for value := range ts.pivots.Iterator() {
			pivot := value.Item.(*adaptix.PivotData)
			usedNames[pivot.PivotName] = true
		}

		ok := false
		for i := 0; i <= 9999; i++ {
			name := fmt.Sprintf("p%d", i)
			if !usedNames[name] {
				pivotData.PivotName = name
				ok = true
				break
			}
		}
		if !ok {
			return fmt.Errorf("the number of pivots has exceeded 9999")
		}
	}

	// ★ 同一 PivotId 只允许存在一条记录。pivot id 由子 agent 的 watermark 派生
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
	}

	if childAgent, err := ts.getAgent(pivotData.ChildAgentId); err == nil {
		childAgent.PivotParent = pivotData
	}

	//emptyMark := ""
	//_ = ts.TsAgentUpdateDataPartial(pivotData.ChildAgentId, struct {
	//	Mark *string `json:"mark"`
	//}{Mark: &emptyMark})

	ts.pivots.Put(pivotData)

	if !isRestore {
		err := ts.DBMS.DbPivotInsert(*pivotData)
		if err != nil {
			logs.Error("", err.Error())
		}
	}

	packet := CreateSpPivotCreate(*pivotData)
	ts.TsSyncAllClients(packet)

	// --- POST HOOK ---
	postEvent := &eventing.EventDataPivotCreate{
		PivotId:       pivotData.PivotId,
		ParentAgentId: pivotData.ParentAgentId,
		ChildAgentId:  pivotData.ChildAgentId,
		PivotName:     pivotData.PivotName,
	}
	ts.EventManager.EmitAsync(eventing.EventPivotCreate, postEvent)
	// -----------------

	return nil
}

func (ts *Teamserver) TsPivotDelete(pivotId string) error {
	// --- PRE HOOK ---
	preEvent := &eventing.EventDataPivotRemove{PivotId: pivotId}
	if !ts.EventManager.Emit(eventing.EventPivotRemove, eventing.HookPre, preEvent) {
		if preEvent.Error != nil {
			return preEvent.Error
		}
		return fmt.Errorf("operation cancelled by hook")
	}
	// ----------------

	pivotData := ts.TsGetPivotById(pivotId)
	if pivotData == nil {
		return fmt.Errorf("pivotId %s does not exist", pivotId)
	}

	if parentAgent, err := ts.getAgent(pivotData.ParentAgentId); err == nil {
		for i := uint(0); i < parentAgent.PivotChilds.Len(); i++ {
			valuePivot, ok := parentAgent.PivotChilds.Get(i)
			if ok {
				if pivot, ok := valuePivot.(*adaptix.PivotData); ok && pivot.PivotId == pivotId {
					parentAgent.PivotChilds.Delete(i)
					break
				}
			}
		}
	}

	if childAgent, err := ts.getAgent(pivotData.ChildAgentId); err == nil {
		childAgent.PivotParent = nil
	}

	unlinkMark := "Unlink"
	_ = ts.TsAgentUpdateDataPartial(pivotData.ChildAgentId, struct {
		Mark *string `json:"mark"`
	}{Mark: &unlinkMark})

	// 删**所有**同 id 条目并同步 DB 行（原来这里 `break` 只删第一条，
	// 同 id 的重复项会永久残留；DbPivotDelete 本身删的就是全部匹配行，
	// 两边语义这才对齐）。这里 withDb=true，保持上游「删内存 + 删 DB」语义。
	ts.tsPivotPurge(pivotId, true)

	packet := CreateSpPivotDelete(pivotId)
	ts.TsSyncAllClients(packet)

	// --- POST HOOK ---
	postEvent := &eventing.EventDataPivotRemove{PivotId: pivotId}
	ts.EventManager.EmitAsync(eventing.EventPivotRemove, postEvent)
	// -----------------

	return nil
}
