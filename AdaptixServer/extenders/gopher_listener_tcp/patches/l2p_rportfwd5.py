#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次9d：rportfwd 监听「同端口重新启用」——补全 core 的 enable/stop 生命周期

    python3 l2p_rportfwd5.py status|check|apply|revert|verify|all

★ 为什么这不是「加功能」，而是「补一个被打破的既有契约」

  core 自己把「停止 → 在同一端口再启用」定义成一等流程，有两处硬证据：
    · TsTunnelStop 的注释：**"Close runtime resources but KEEP tunnel entry for re-enable."**
      （只关运行态资源、**故意保留** entry 以便再启用）
    · TsTunnelCreate:432-443 的 reuse 分支：
      `if existingTunnel.Active { return ErrTunnelAlreadyActive }`
      "Reuse inactive entry: refresh metadata, keep id for enable/stop lifecycle."
      ⇒ 同端口再启用时**不报错**，复用同一 TunnelId（= CRC32(client+agent+"rportfwd"+port)），
        随后 TsTunnelStart 的 REVERSE 分支**重新下发** Callbacks.Reverse ⇒ agent 会再收到一次
        TunnelMessageReverse。

  ⇒ 但隧道 stop 时 agent **收不到任何通知**（三层都断，见下），旧 listener 仍在
    ⇒ agent 的 `net.Listen` 必然 EADDRINUSE ⇒ 回 Result:0
    ⇒ core 走 TsTunnelUpdateRportfwd(tunnelId, false) ⇒ **DeleteTunnel**
    ⇒ 操作员看到的是「GUI 上重新启用直接失败、隧道消失」，**而 core 的设计意图是成功**。

  ★ J6 三层断裂（本批次逐行核实，均**不是** agent 的锅，所以也不在 agent 侧修）：
    ① 协议层：`adaptix.TunnelCallbacks`（外部模块 axc2@v1.2.0）只有
       ConnectTCP/ConnectUDP/WriteTCP/WriteUDP/Pause/Resume/Close(channelId)/Reverse，
       **没有**任何「关监听」成员 ⇒ 类型系统表达不了这件事。
    ② core 层：`TsTunnelStop` 只关 `tunnel.listener`（那是 lportfwd/socks 的**服务端**监听），
       反向的监听在 agent 上 ⇒ 这一段对 REVERSE 是空操作。
    ③ 死钩子：`TsTunnelStopRportfwd`（ts_tunnels.go:597）**全树零调用者**，而且它把
       `int(id)`（TunnelId）传给了 `Callbacks.Close(channelId)` —— **实参语义就是错的**
       （Close 的形参是 channelId，见 axc2 adaptix_struct.go:100）⇒ 即便被调用也只会
       让 agent 去关一个不存在的 channel。
    ⇒ 结论：J6「stop 后端口不释放」是**上游协议级缺失**，要真修必须改 axc2 模块 +
       core + **全部 20 个 extender 与 server 二进制一起重建**（Go plugin 有包哈希校验）
       ⇒ 本批次**明确不做**，登记为开放项（见 e2e 的 KNOWN_OPEN）。

★ 本批次做的（agent 单侧、零 core 改动、零协议改动）：
     agent 记一张「本 agent 自己建立的 rportfwd 监听」表（port → net.Listener）。
     收到同端口的新 Reverse 任务时，若 `net.Listen` 失败**且占用者是本 agent 为该端口
     建的旧 listener**，就关掉它、**重试一次**。
     · 只关**自己建的**：外来占用者照旧失败（负向判据 J1b 依赖这一点，不得削弱）。
     · 顺带回收：那条陈旧 accept 循环随之退出 ⇒ J6 的泄漏在「再启用」时被回收。

     判据 X1–X8 见 verify：X5 断言「先 Take 再 Close 再 Listen」的顺序与**条件性**
     （必须包在 if err != nil 之内，否则会变成无条件抢占端口）。
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys

FILE = "/root/cc/AdaptixServer/extenders/gopher_agent/src_gopher/tasks.go"
BAKDIR = "/root/gp5probe/bak"
SUF = ".bak-rpf5"
GOFMT = "/usr/local/go/bin/gofmt"
OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"

HELPERS = """// ── axRpfLns：本 agent 自己建立的 rportfwd 监听（port → net.Listener）────────────
// ★ 为什么需要这张表：隧道 stop 时 core **无法通知 agent** 关监听 ——
//   ① axc2 的 adaptix.TunnelCallbacks 里没有「关监听」这个成员（只有 Close(channelId)）；
//   ② TsTunnelStop 只关服务端的 tunnel.listener，反向的监听在 agent 上，对 REVERSE 是空操作；
//   ③ 上游留的 TsTunnelStopRportfwd 全树零调用者，且把 TunnelId 当 channelId 传（实参语义错）。
//   ⇒ 旧 listener 会一直占着端口。有了这张表就能：
//     ① 补全 core 的 enable/stop 生命周期：同端口再启用时**只关自己建的**那个旧 listener，
//        让 `TsTunnelCreate` 的 reuse 分支（"keep id for enable/stop lifecycle"）真正可用；
//     ② 绝不去关别人的监听 —— 外来占用者照旧走 Result:0 的失败路径。
// ★ 用 Mutex + map 而不是 sync.Map：删除必须「按身份条件删」（只删还是自己的那条），
//   sync.Map 在不依赖 CompareAndDelete（Go 版本相关）的前提下表达不了这个语义。
// ★ 注意上面这行与 `var (` 之间的**空行是必需的**：Go 1.19+ 的 gofmt 会把「紧贴声明的
//   注释块」当 doc comment 重排（把 `//   xxx` 改成 `//\txxx`）⇒ 判据 X7 会红。
//   空行让注释与声明脱钩，gofmt 就不动它（这是本批次实测出来的，见归档坑位）。

var (
	axRpfLns   = make(map[int]net.Listener)
	axRpfLnsMu sync.Mutex
)

func axPutRpfListener(port int, ln net.Listener) {
	axRpfLnsMu.Lock()
	old := axRpfLns[port]
	axRpfLns[port] = ln
	axRpfLnsMu.Unlock()
	if old != nil && old != ln {
		_ = old.Close() // 同端口被顶替：关掉上一个，避免泄漏
	}
}

func axTakeRpfListener(port int) (net.Listener, bool) {
	axRpfLnsMu.Lock()
	old, ok := axRpfLns[port]
	delete(axRpfLns, port)
	axRpfLnsMu.Unlock()
	return old, ok
}

func axDelRpfListener(port int, ln net.Listener) {
	axRpfLnsMu.Lock()
	if cur, ok := axRpfLns[port]; ok && cur == ln {
		delete(axRpfLns, port)
	}
	axRpfLnsMu.Unlock()
}

"""

RETRY = """		// ★ 同端口重新启用（stop → start）：core 保留 inactive entry 并**重新下发**本任务
		//   （ts_tunnels.go:432-443 "keep id for enable/stop lifecycle"），但 stop 时
		//   core 通知不到 agent（见 axRpfLns 的注释）⇒ 旧 listener 仍在 ⇒ 这里 EADDRINUSE。
		//   ⇒ 若占用者**是本 agent 为同一端口建的旧 listener**，关掉它再试一次；
		//     外来占用者（别人的进程）**仍然失败** —— 这是判据 J1b 的前提，不得放宽。
		if old, ok := axTakeRpfListener(params.Port); ok {
			_ = old.Close()
			ln, err = net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", params.Port))
		}
"""

EDITS = [
    dict(tag="B1 监听表 + 三个 helper", kind="ins",
         a="// axRandChannelId 生成反向通道的 channelId。\n", b=None,
         new=HELPERS + "// axRandChannelId 生成反向通道的 channelId。\n",
         fp="axRpfLns   = make(map[int]net.Listener)"),
    dict(tag="B2 失败时的自愈重试", kind="ins",
         a="\t\t// 监听失败也要回结果", b=None,
         new=RETRY + "\t}\n\tif err != nil {\n\t\t// 监听失败也要回结果",
         fp="if old, ok := axTakeRpfListener(params.Port); ok {"),
    dict(tag="B3 登记/注销", kind="ins",
         a="\n\tgo func() {\n\t\tfor {\n", b=None,
         new=("\n\taxPutRpfListener(params.Port, ln)\n\n"
              "\tgo func() {\n\t\tdefer axDelRpfListener(params.Port, ln)\n\t\tfor {\n"),
         fp="axPutRpfListener(params.Port, ln)"),
]


def rd(p):
    return io.open(p, encoding="utf-8").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="\n").write(s)


def md5(p):
    return hashlib.md5(io.open(p, "rb").read()).hexdigest()


def bakpath():
    return os.path.join(BAKDIR, "gopher_src.tasks.go" + SUF)


def applied(e, s):
    return s.count(e["fp"]) == 1


def apply_one(e, s):
    if applied(e, s):
        return s, None
    if s.count(e["a"]) != 1:
        return s, "锚点不唯一（%d）" % s.count(e["a"])
    return s.replace(e["a"], e["new"], 1), None


def gofmt_lines(p):
    try:
        r = subprocess.run([GOFMT, "-d", p], capture_output=True, text=True, timeout=60)
        return len(r.stdout.split("\n"))
    except Exception:
        return -1


def cmd_status():
    s = rd(FILE)
    print("== 现状 ==  %s  md5=%s size=%d" % (os.path.basename(FILE), md5(FILE), os.path.getsize(FILE)))
    for e in EDITS:
        print("  %-26s 已应用=%s" % (e["tag"], applied(e, s)))


def cmd_check():
    print("== LINT ==")
    s, ok = rd(FILE), True
    for e in EDITS:
        n, good = s.count(e["a"]), (applied(e, s) or s.count(e["a"]) == 1)
        ok &= good
        print("  %s %-26s 锚=%d 已应用=%s" % (OK if good else BAD, e["tag"], n, applied(e, s)))
    print("  %s LINT: %s" % (OK if ok else BAD, "OK" if ok else "FAIL"))
    print("  %s %d/%d todo" % (INFO, sum(1 for e in EDITS if not applied(e, s)), len(EDITS)))
    return ok


def cmd_apply():
    print("== APPLY ==")
    b = bakpath()
    if not os.path.exists(b):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(FILE, b)
        print("  %s 回滚点 %s（md5=%s）" % (OK, os.path.basename(b), md5(b)[:12]))
    else:
        print("  %s 回滚点已存在（md5=%s）" % (INFO, md5(b)[:12]))
    s = rd(FILE)
    for e in EDITS:
        s2, err = apply_one(e, s)
        if err:
            print("  %s %-26s %s ⇒ 拒绝（不动盘）" % (BAD, e["tag"], err))
            return False
        s = s2
        print("  %s %-26s ok" % (OK, e["tag"]))
    wr(FILE, s)
    print("  %s APPLY 完成：%d 处" % (OK, len(EDITS)))
    return True


def cmd_revert():
    b = bakpath()
    if not os.path.exists(b):
        print("  %s 缺回滚点 %s" % (BAD, b))
        return False
    shutil.copy2(b, FILE)
    print("  %s 已还原（md5=%s）" % (OK, md5(FILE)[:12]))
    return True


def cmd_verify():
    print("== VERIFY ==")
    ok, s, b = True, rd(FILE), bakpath()

    u1 = all(applied(e, s) for e in EDITS)
    ok &= u1
    print("  %s X1 三处编辑全部到位（%d/3）" % (OK if u1 else BAD, sum(1 for e in EDITS if applied(e, s))))

    u2 = False
    if os.path.exists(b):
        t = rd(b)
        for e in EDITS:
            t, err = apply_one(e, t)
            if err:
                t = None
                break
        u2 = (t == s)
    ok &= u2
    print("  %s X2 回滚点是可正向重放的 pre-image" % (OK if u2 else BAD))

    # X3 ★ 数「调用点」而不是「名字出现次数」（批次9 坑2 的教训）
    def cnt(x):
        return s.count(x)
    u3 = (cnt("func axPutRpfListener(") == 1 and cnt("axPutRpfListener(params.Port, ln)") == 1
          and cnt("func axTakeRpfListener(") == 1 and cnt("ok := axTakeRpfListener(params.Port); ok") == 1
          and cnt("func axDelRpfListener(") == 1 and cnt("defer axDelRpfListener(params.Port, ln)") == 1)
    ok &= u3
    print("  %s X3 三函数：定义各 1 ＋ 调用点各 1（按完整签名计数）" % (OK if u3 else BAD))

    # X4 ★ 顺序判据：Take → Close → Listen（重试块内），且 Take 在 Listen 之前
    i_take = s.find("if old, ok := axTakeRpfListener(params.Port); ok {")
    i_close = s.find("_ = old.Close()", i_take)
    i_reln = s.find('ln, err = net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", params.Port))', i_close)
    u4 = (0 <= i_take < i_close < i_reln)
    ok &= u4
    print("  %s X4 重试块顺序：Take(%d) < Close(%d) < 重试 Listen(%d)"
          % (OK if u4 else BAD, i_take, i_close, i_reln))

    # X5 ★★ 关键：重试必须是**条件性**的（包在 if err != nil 内），且失败路径仍在
    #       —— 无条件重试 = 抢占别人的端口 = 判据 J1b 会被洗绿
    #   ★ 取整段函数体，不能截到 axPutRpfListener：`Result: 1` 在它**之后**
    #     （v1 就是因为截断了才报假红 —— 判据的切片边界要和它断言的东西对齐）
    body = s[s.find("func taskTunnelReverse("):]
    body = body[:body.find("\n}\n") + 3]
    first_if = body.find("if err != nil {")
    retry_at = body.find("axTakeRpfListener(params.Port)")
    fail_ret = body.find("AnsTunnelReverse{TunnelId: params.TunnelId, Result: 0}")
    ok_ret = body.find("Result: 1")
    u5 = (0 <= first_if < retry_at) and (fail_ret > retry_at) and (ok_ret > 0)
    ok &= u5
    print("  %s X5 重试被包在首次 if err != nil 内（first_if=%d < retry=%d），"
          "Result:0 失败路径仍在（%d），成功路径仍在（%d）"
          % (OK if u5 else BAD, first_if, retry_at, fail_ret, ok_ret))

    # X6 不得引入新的 import / 不得动 chan/其它判据
    u6 = ("axRpfLnsMu sync.Mutex" in s) and ("\t\"sync\"\n" in s)
    ok &= u6
    print("  %s X6 复用本文件既有 import（sync），未新增依赖" % (OK if u6 else BAD))

    # X7 gofmt 零新增（文件本来就非全合规，只负责 delta=0）
    pre = gofmt_lines(b) if os.path.exists(b) else -1
    post = gofmt_lines(FILE)
    u7 = (pre >= 0 and pre == post)
    ok &= u7
    print("  %s X7 gofmt 零新增（pre=%d post=%d delta=%d）" % (OK if u7 else BAD, pre, post, post - pre))
    if not u7:
        # ★ 失败时**必须把差异打出来**：否则要再跑一轮才知道差在哪（本批次踩过）
        try:
            r = subprocess.run([GOFMT, "-d", FILE], capture_output=True, text=True, timeout=60)
            for ln in r.stdout.split("\n")[:14]:
                print("      | %s" % ln[:150])
        except Exception:
            pass

    # X8 关键函数仍在（没被改动结构）
    u8 = ("func taskTunnelReverse(" in s) and ("axRandChannelId() int {" in s)
    ok &= u8
    print("  %s X8 既有结构完好（taskTunnelReverse / axRandChannelId 均在）" % (OK if u8 else BAD))

    print("  %s RESULT: %s" % (OK if ok else BAD, "PASS" if ok else "FAIL"))
    return ok


def main():
    m = sys.argv[1] if len(sys.argv) > 1 else "status"
    if m == "all":
        sys.exit(0 if (cmd_check() and cmd_apply() and cmd_verify()) else 1)
    f = dict(status=cmd_status, check=cmd_check, apply=cmd_apply,
             revert=cmd_revert, verify=cmd_verify).get(m)
    if not f:
        print(__doc__)
        sys.exit(2)
    r = f()
    sys.exit(0 if (r is None or r) else 1)


if __name__ == "__main__":
    main()
