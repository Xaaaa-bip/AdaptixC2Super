#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 §4 步骤 7：让 teamserver 能**编出** libp2p 变体的 agent。

    python3 patches/libp2p_switch.py check|apply|verify|revert

改的是 **teamserver 侧插件** `pl_main.go`（不是 agent 源码），三处**文本锚点**替换：

  A. `GenerateProfiles` 的 `switch protocol` 里加 `case "libp2p":` —— 把 libp2p 的
     多地址/peer id/protocol/私钥/拨号超时写进 Profile（`Transport = "libp2p"`）。
     ★ 与 `case "tcp":` 共用 `callback_addresses` 的解析，但**不套 TLS**：Noise 已提供
     加密与对端认证（见 `src_gopher/transport.go` 的 `DialProfile`）。
  B. `BuildPayload` 里：只要**任一** listener profile 的 `protocol == "libp2p"`，
     就往 `cmdBuild` 追加 `-tags libp2p`。否则编进去的是 `transport_nolibp2p.go`
     的**替身** ⇒ agent 一拨号就拿到 "this build has no libp2p support"，
     而 operator 在 UI 上只看到"上线失败"（**这正是 §4.2 那个 build tag 决策的代价**：
     收益是默认产物 0 字节膨胀，代价是**必须有人负责把 tag 加上**）。
  C. 新增 `agentNeedsLibp2p(profile)` 助手。

★ 判据取自 `profile.ListenerProfiles`（与 `GenerateProfiles` 同一来源），**不用包级变量** ——
  teamserver 里两个并发构建会互相污染（`SrcPath` 那种包级变量在这里是有害的）。
★ 追加位置在命令**末尾**（`… -o <path> -tags libp2p`）：实测 `go build` 无位置参数时
  接受尾部 flag，且产物与前置写法**逐字节相同**（26,505,400）。★ 必须**两个分支都覆盖** ——
  Win7 分支（`generateConfig.Win7support`）会把 `cmdBuild` **整个重写**，所以注入点必须在
  它**之后**，否则 Win7 + libp2p 组合静默退化成替身。

★★ 没有 git：本脚本 + `.bak-switch` 是唯一回滚手段，REVERT 段与 APPLY 段写在一起。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "pl_main.go")
BAK = ".bak-switch"

# A 的锚点：协议 switch 的 default 分支（全文件唯一）。
# ★ 缩进是 **2 tab**（switch 在 func 体 1 级 + for 体 1 级），case 体 3 tab ——
#   `cat -A` 实测：`^I^Idefault:` / `^I^I^Ireturn nil, errors.New("protocol unknown")`。
#   不要凭工具渲染的观感猜 tab 数（渲染会把 tab 压扁，我这里就先猜错了一次）。
A_ANCHOR = '\t\tdefault:\n\t\t\treturn nil, errors.New("protocol unknown")\n'

# B 的锚点：构建日志（全文件唯一）。注入点必须在 Win7 分支之后。
B_ANCHOR = '\t_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Starting build process...")\n'

# C 的锚点：BuildPayload 函数头（全文件唯一）
C_ANCHOR = 'func (p *PluginAgent) BuildPayload(profile adaptix.BuildProfile, agentProfiles [][]byte) ([]byte, string, error) {\n'


def detab(s, base=0):
    """把每 4 个前导空格换成 1 个 tab（先整体左移 base 个空格）—— Go 源码必须 tab 缩进。

    ★ 三引号字面量自带的首尾空行会被 `strip("\\n")` 吃掉、并**补回恰好一个**结尾换行 ——
      否则插入后会多出一个空行（实测：gofmt 会来删它，虽然这棵树本来就 `gofmt -l` 红，
      但"我新加的行不被 gofmt 触碰"是一条值得保持的不变量，见归档「归因判据」）。
    """
    out = []
    for ln in s.strip("\n").split("\n"):
        if not ln.strip():
            out.append("")
            continue
        n = len(ln) - len(ln.lstrip(" "))
        if n < base or (n - base) % 4:
            raise ValueError("缩进 %d 与 base=%d 不匹配: %r" % (n, base, ln))
        out.append("\t" * ((n - base) // 4) + ln[n:])
    return "\n".join(out) + "\n"


# ★ base=4：下面这块按**比目标深一级**书写（为了在源码里读起来层次清楚），
#   因为 `case` 标签在 switch 里是 2 tab、case 体是 3 tab，手写时容易少一级。
CASE_LIBP2P = detab('''
            case "libp2p":

                // Phase5 §4 步骤 7。地址语义与 tcp 分支共用 callback_addresses
                // （此处是 multiaddr，不是 host:port），但**不套 TLS** —— Noise 已提供
                // 加密与对端认证，再叠一层只会增加握手延迟与签名面。
                servers, ok := listenerMap["callback_addresses"].(string)
                if !ok {
                    return nil, errors.New("libp2p profile: callback_addresses must be a string")
                }

                servers = strings.ReplaceAll(servers, " ", "")
                servers = strings.ReplaceAll(servers, "\\n", ",")
                servers = strings.TrimSuffix(servers, ",")
                var addrs []string
                for _, a := range strings.Split(servers, ",") {
                    a = strings.TrimSpace(a)
                    if a != "" {
                        addrs = append(addrs, a)
                    }
                }

                // 可选 l2p_addrs：显式多地址列表（给了就**覆盖** callback_addresses）。
                if extra, ok := listenerMap["l2p_addrs"].(string); ok && strings.TrimSpace(extra) != "" {
                    var fromExtra []string
                    for _, a := range strings.FieldsFunc(extra, func(r rune) bool {
                        return r == ',' || r == '\\n' || r == ' ' || r == '\\t'
                    }) {
                        if a = strings.TrimSpace(a); a != "" {
                            fromExtra = append(fromExtra, a)
                        }
                    }
                    if len(fromExtra) > 0 {
                        addrs = fromExtra
                    }
                }
                if len(addrs) == 0 {
                    return nil, errors.New("libp2p profile: need at least one multiaddr")
                }

                l2pPeerID, _ := listenerMap["l2p_peer_id"].(string)
                l2pProtocol, _ := listenerMap["l2p_protocol"].(string)
                l2pTimeout := 0
                if v, ok := listenerMap["l2p_dial_timeout"].(float64); ok && v > 0 {
                    l2pTimeout = int(v)
                }
                var l2pPrivKey []byte
                if k, ok := listenerMap["l2p_privkey"].(string); ok && k != "" {
                    l2pPrivKey, err = base64.StdEncoding.DecodeString(k)
                    if err != nil {
                        return nil, fmt.Errorf("libp2p profile: bad l2p_privkey base64: %w", err)
                    }
                }

                profile := Profile{
                    Type:              uint(agentWatermark),
                    Addresses:         addrs,
                    ConnTimeout:       reconnectTimeout,
                    ConnCount:         generateConfig.ReconnectCount,
                    UseSSL:            false,
                    Transport:         "libp2p",
                    Libp2pPeerID:      l2pPeerID,
                    Libp2pAddrs:       addrs,
                    Libp2pPrivKey:     l2pPrivKey,
                    Libp2pProtocol:    l2pProtocol,
                    Libp2pDialTimeout: l2pTimeout,
                }
                profileData, err = msgpack.Marshal(profile)
                if err != nil {
                    return nil, err
                }

''', base=4)

HELPER = detab('''
// agentNeedsLibp2p 判断这次构建是否需要 `-tags libp2p`（Phase5 §4 步骤 7）。
//
// ★ 判据必须与 GenerateProfiles 看**同一个来源**（profile.ListenerProfiles），
// 而不是包级变量 —— teamserver 可以并发构建，包级变量会让"A 构建的 tag 泄漏到 B"。
// ★ 与 GenerateProfiles 里的 switch **故意不共用函数**：这里只做"有没有 libp2p"
// 这一件事，不解析 profile 内容；解析失败当"不需要"处理，真正的错误留给
// GenerateProfiles 去报，避免同一个错误在两处各报一遍。
// （↑ 续行**不缩进**：Go 1.19+ 会把 doc comment 里缩进的续行当成代码块重排成 `//\t`。）
func agentNeedsLibp2p(profile adaptix.BuildProfile) bool {
    for _, lp := range profile.ListenerProfiles {
        var lm map[string]any
        if err := json.Unmarshal(lp.Profile, &lm); err != nil {
            continue
        }
        if proto, _ := lm["protocol"].(string); proto == "libp2p" {
            return true
        }
    }
    return false
}

''')

TAG_INJECT = detab('''
    // Phase5 §4 步骤 7：任一 listener profile 要 libp2p ⇒ 本产物必须带 tag 构建，
    // 否则编进去的是 transport_nolibp2p.go 的替身，agent 拨号必失败。
    // ★ 注入点**必须**在 Win7 分支**之后**：那个分支会把 cmdBuild 整个重写。
    if agentNeedsLibp2p(profile) {
        cmdBuild += " -tags libp2p"
        _ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: libp2p transport requested -> adding -tags libp2p")
    }

''')


def state(t):
    n_case = t.count('case "libp2p":')
    n_tag = t.count('cmdBuild += " -tags libp2p"')
    n_help = t.count("func agentNeedsLibp2p(")
    n_call = t.count("if agentNeedsLibp2p(profile) {")
    if n_case == 0 and n_tag == 0 and n_help == 0:
        return "old", (n_case, n_tag, n_help, n_call)
    if n_case == 1 and n_tag == 1 and n_help == 1 and n_call == 1:
        return "done", (n_case, n_tag, n_help, n_call)
    return "???", (n_case, n_tag, n_help, n_call)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    if not os.path.exists(SRC):
        print("[BAD ] 找不到 %s" % SRC)
        return 2
    t = open(SRC, encoding="utf-8").read()
    s, counts = state(t)
    print("[%-4s] pl_main.go   case=%d tag=%d helper=%d call=%d" % ((s,) + counts))

    if mode == "check":
        if s == "old":
            for name, a in (("A default 分支", A_ANCHOR), ("B 构建日志", B_ANCHOR),
                            ("C BuildPayload 函数头", C_ANCHOR)):
                print("  [%s] 锚点 %s（出现 %d 次）"
                      % ("ok  " if t.count(a) == 1 else "BAD ", name, t.count(a)))
        print("\ncheck: %s" % ("锚点匹配（%s）" % s if s != "???" else "状态不明 —— 人工看一眼"))
        return 0 if s != "???" else 1

    if mode == "apply":
        if s == "???":                                   # 状态不明 ⇒ 一个字节都不写
            print("[BAD ] 状态不明 —— 拒绝落盘")
            return 1
        if s == "done":
            print("[done] 已是目标态")
            return verify()
        for name, a in (("A", A_ANCHOR), ("B", B_ANCHOR), ("C", C_ANCHOR)):
            if t.count(a) != 1:
                print("[BAD ] 锚点 %s 出现 %d 次（应 1）—— 拒绝落盘" % (name, t.count(a)))
                return 1
        shutil.copy2(SRC, SRC + BAK)
        # 串行累积：后一处替换基于前一处**已改好的**文本（见归档「原子≠串行」）
        t = t.replace(A_ANCHOR, CASE_LIBP2P + "\n" + A_ANCHOR, 1)
        t = t.replace(C_ANCHOR, HELPER + "\n" + C_ANCHOR, 1)
        t = t.replace(B_ANCHOR, TAG_INJECT + "\n" + B_ANCHOR, 1)
        open(SRC, "w", encoding="utf-8").write(t)
        print("[ok  ] 已改（备份 pl_main.go" + BAK + "）")
        return verify()

    if mode == "revert":
        if not os.path.exists(SRC + BAK):
            print("[BAD ] 缺 pl_main.go%s —— 拒绝 revert（不许假装成功）" % BAK)
            return 1
        shutil.move(SRC + BAK, SRC)
        print("[ok  ] 已还原")
        return 0

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


def verify():
    t = open(SRC, encoding="utf-8").read()
    # ★ 去掉全部空白后再比对：这两个字段的字面量在 struct 里是**对齐**的，
    #   而对齐宽度会被 gofmt 按最长字段名重排 ⇒ 直接比字面量串是"判据自己算错"那一族。
    flat = "".join(t.split())
    # ★ B 的注入点必须在 Win7 分支**之后**（否则 Win7+libp2p 静默退化成替身）
    i_win7 = t.find('cmdBuild = fmt.Sprintf("GOWORK=off CGO_ENABLED=0 GOOS=%s GOARCH=%s GOROOT=/usr/lib/go-win7/')
    i_inj = t.find('cmdBuild += " -tags libp2p"')
    checks = [
        ('case "libp2p": 恰好 1 处', t.count('case "libp2p":') == 1),
        ("agentNeedsLibp2p 已定义", t.count("func agentNeedsLibp2p(") == 1),
        ("agentNeedsLibp2p 确被调用", t.count("if agentNeedsLibp2p(profile) {") == 1),
        ('cmdBuild 追加 -tags libp2p', t.count('cmdBuild += " -tags libp2p"') == 1),
        ("注入点在 Win7 分支之后", i_win7 >= 0 and i_inj > i_win7),
        ("Transport 被置为 libp2p", 'Transport:"libp2p",' in flat),
        ("libp2p 分支不套 TLS", "UseSSL:false," in flat),
        ("default 分支仍在（未知协议仍报错）", t.count('return nil, errors.New("protocol unknown")') == 1),
    ]
    for name, ok in checks:
        print("[%s] %s" % ("ok  " if ok else "BAD ", name))
    bad = sum(1 for _, ok in checks if not ok)
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
