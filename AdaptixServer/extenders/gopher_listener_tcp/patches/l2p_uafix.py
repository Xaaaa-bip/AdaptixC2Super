#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次 8：把 libp2p 的 identify user-agent（agentVersion）从**默认自曝值**改成中性值。

## 为什么要改（实测，不是推测）

libp2p 的默认 user-agent 不是固定串：

    // p2p/protocol/identify/user_agent.go:20 / :39
    defaultUserAgent = fmt.Sprintf("%s@%s", bi.Main.Path, bi.Main.Version|revision)

`bi.Main` 是**编译该二进制的模块本身**，因此同一项目的不同部件会报出**互不相同**的自曝
串 —— 对端据此甚至能区分对方是哪个部件。三枚产物实测（`go version -m`，与 bi.Main 同源）：

    交付的 agent（dist/src_gopher） path=gopher                → "gopher@"        ← 被测对象
    agent_gopher.so（builder 插件） path=adaptix_agent_gopher  → "adaptix_agent_gopher@"
    listener_gopher_tcp.so         path=command-line-arguments → "command-line-arguments@"

（`/root/cc/AdaptixServer` **不是 git 仓库** ⇒ vcs.revision 缺失 ⇒ revision 为空串，
`-dirty` 也不会加上 ⇒ 值以 `@` 结尾。）
★ 我起初据 `go version -m agent_gopher.so` 把 "adaptix_agent_gopher@" 当成了 agent 的默认值
—— **错的**：那枚是 teamserver 侧的 builder 插件。真正投递到目标机的是 pl_main.go 从
`dist/src_gopher` 现编的那枚（模块 `gopher`）。静态读单个产物只能得到**那一种**构建方式下
的值，这是本批次踩过的认知陷阱。

⇒ 不设 UserAgent，每次 identify 都在向对端自报产品名 / 自报构建方式，且是生态里
**不存在**的畸形串。这是**指纹**，不是"细节"。

## 改法

三处生产 host 构造点各加一行 `libp2p.UserAgent(axLibp2pUserAgent)`，并在各文件里定义
同名常量，取值 **`go-libp2p`**（主流前缀、无版本号、无产品名、非伪装）。

三处必须**逐字节相同**：同一个 C2 的两端（甚至 relay）报出不同指纹，等于自画关系图。

## ★★★ 判据的独立性（第 11 形态假绿）

动态判据在 `gopher_agent/src_gopher/ax_ua_test.go`，它用**独立字面量** `axUAWant`
锚定中性值，**不引用**生产常量 —— 否则把常量改成 `"adaptix/x"` 时判据照样绿（重言式）。
本脚本的 `verify` 因此要判**四个锚点**（三处生产常量 + 测试里的 axUAWant）**逐字节相同**：
少判一个，漂移就会从那个口子漏过去。

用法：
    l2p_uafix.py status    # 只报告七处当前处于哪一态（默认）
    l2p_uafix.py check     # 落盘前自检：锚点唯一、new 里含 old、无自相矛盾
    l2p_uafix.py apply     # 落盘（幂等；回滚点写 BAKDIR）
    l2p_uafix.py revert    # 从回滚点还原（与 apply 成对，见文件尾）
    l2p_uafix.py verify    # 判据：静态四锚点一致性 + 反指纹 + 无遗漏 + 动态 go test
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True   # ★ 批次13：本文件会 import l2p_relay_e2e

EXT = "/root/cc/AdaptixServer/extenders"
LST = os.path.join(EXT, "gopher_listener_tcp")
AGT = os.path.join(EXT, "gopher_agent")
BAKDIR = "/root/gp5probe/bak"

T_AGENT = os.path.join(AGT, "src_gopher", "transport_libp2p.go")
T_LST = os.path.join(LST, "pl_libp2p.go")
T_RELAY = os.path.join(LST, "relaynode", "main.go")
# ★ 第 7 处：relay e2e 判据里的**手抄件**（对照 C2 节点的 opts）。它不是生产代码、不进
#   任何产物，但 l2p_relay_e2e.py 的 check() 判 `生产选项 ⊆ 抄件`（顺序一致、抄件不许漏）——
#   生产加了 UserAgent 而抄件不跟 ⇒ **那条判据翻红**。实测证实：批次8 前抄件与 pl 逐项相同。
T_COPY = os.path.join(AGT, "src_gopher", "ax_relay_e2e_test.go")

# ★ 动态判据文件（不参与任何 .so 的构建 —— _test.go 从不编进 plugin/可执行体）。
TESTFILE = os.path.join(AGT, "src_gopher", "ax_ua_test.go")

UA = "go-libp2p"

# ---------------------------------------------------------------- 七处替换
# 顺序即 apply 顺序；每项 (文件, 锚点 old, 目标态 new)。old 必须**恰好出现一次**。
# ★ opts 那三处的 old 文本在 listener 与 relaynode 里**完全相同**（同为两个 tab）——
#   靠文件路径区分，因此 lint() 还要检查"没有两项落在同一文件的同一锚点上"。

# ★ 两条模板纪律（都是实测踩过才写下的）：
#
# ① 占位符用 `__UA__` + replace，**不用** `%s` 格式化：下面这段注释要**原样引用**
#    libp2p 自己的源码写法 `fmt.Sprintf("%s@%s", ...)`，用 `%` 格式化会把那两处
#    当成占位符 ⇒ "not enough arguments for format string"。
#    同族陷阱：带 `{}` 的模板不要用 str.format / f-string。
#
# ② **doc comment 的续行一律不缩进**（不写 `//   xxx`，写 `// xxx`），列表用
#    `①②③` 而不是 `- `。原因：Go 1.19+ 的 gofmt 把文档注释里**缩进的行**当成
#    代码块（要求前置空行 + tab 缩进），把 `- ` 之后的续行要求对齐到列表内容列
#    —— 于是 `gofmt -l` 会报这个文件，而**编译与构建都毫无提示**。
#    ★ 缩进在**块内**注释（如 const 块、函数体内的行注释）里不受此规则约束，
#      所以 relaynode 那处带 tab 的模板本来就没问题。
COMMENT_AGENT_CONST = '''// axLibp2pUserAgent 是 identify 协议报出的 agentVersion（= libp2p 的 user-agent）。
//
// 为什么必须显式设：libp2p 的默认值不是固定串，而是
// `fmt.Sprintf("%s@%s", bi.Main.Path, 版本|revision)`
// （p2p/protocol/identify/user_agent.go:20/:39）—— bi.Main 是**编译该二进制的模块本身**。
// 本 agent 实测 Main.Path = "gopher"（src_gopher/go.mod 的 module 名）+ 版本 "(devel)"
// + 非 git 仓库（revision 恒空）⇒ 默认值 = "gopher@" —— 既**写出产品名**，又是生态里
// 不存在的畸形串（以 @ 结尾且 revision 为空）。不设它 = 每次 identify 都向对端自报家门。
// ★ Main.Path 随**构建方式**而变，同一项目的不同部件报出**互不相同**的自曝串：builder
// 插件报它的模块名、plugin 对文件列表直接编译时报 "command-line-arguments@" —— 对端据此
// 甚至能区分对方是哪个部件。静态读某一枚产物的 `go version -m` 只能得到那一种值。
//
// 取值纪律（三处生产代码必须**逐字节相同**，由 l2p_uafix.py verify 判）：
// ① 用**主流前缀** go-libp2p，混进生态人群，不制造本项目独有特征；
// ② **不带版本号**：版本号是对端反查已知漏洞的线索；且本项目用的是 third_party
// 私有 replace 的 libp2p，报任何版本号都是**假信息**；
// ③ **不放产品名/构建方式**（adaptix / gopher / axc2 / command-line-arguments）。
// 刻意**不**伪装成别的实现（kubo/…、rust-libp2p/…）：冒充真实软件且行为不符，更可疑。
const axLibp2pUserAgent = "__UA__"
'''.replace("__UA__", UA)

COMMENT_LST_CONST = '''// axLibp2pUserAgent 见 agent 侧 transport_libp2p.go 的同名常量（同一取值纪律）。
// listener 不设时的默认值实测是 "command-line-arguments@"（plugin 直接对文件列表
// 编译 ⇒ bi.Main.Path 就是它）—— 生态里绝无此值，本身就是强可疑信号。
// 与 agent 侧**必须逐字节相同**：同一 C2 两端报出不同指纹 = 自画关系图。
const axLibp2pUserAgent = "__UA__"
'''.replace("__UA__", UA)

COMMENT_RELAY_CONST = '''\t// axLibp2pUserAgent 见 agent 侧 transport_libp2p.go 的同名常量（同一取值纪律）。
\t// ★ relaynode 是**最可能被公网扫到/被拨**的那个（relay 服务器本就暴露在外），
\t//   它的 identify 指纹比另两处更早、更容易被记录，默认值同样是自曝串。
\t// ★ 三处必须逐字节相同，由 l2p_uafix.py verify 判。
\taxLibp2pUserAgent = "__UA__"
'''.replace("__UA__", UA)

TEXTS = [
    # ── agent：常量 + opts ──
    (T_AGENT,
     'const defaultLibp2pDialTimeout = 30 * time.Second\n',
     'const defaultLibp2pDialTimeout = 30 * time.Second\n\n' + COMMENT_AGENT_CONST),
    (T_AGENT,
     '\t\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n',
     '\t\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n'
     '\t\t\t// ★ 批次8：identify 报出的 agentVersion。不设 ⇒ 默认值是 "<模块路径>@"\n'
     '\t\t\t//   （bi.Main.Path + 空 revision，非 git 构建下 revision 恒空）。★ 交付的\n'
     '\t\t\t//   这枚 agent 实测 Main.Path="gopher" ⇒ 默认值就是 "gopher@" 这个自曝串。\n'
     '\t\t\tlibp2p.UserAgent(axLibp2pUserAgent),\n'),
    # ── listener：常量 + opts ──
    (T_LST,
     'const axDefaultLibp2pProtocol = "/gopher/1.0.0"\n',
     'const axDefaultLibp2pProtocol = "/gopher/1.0.0"\n\n' + COMMENT_LST_CONST),
    (T_LST,
     '\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n',
     '\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n'
     '\t\t// ★ 批次8：identify 报出的 agentVersion（见 axLibp2pUserAgent 定义处）。\n'
     '\t\tlibp2p.UserAgent(axLibp2pUserAgent),\n'),
    # ── relaynode：常量（进既有 const 块尾）+ opts ──
    # ★ old 刻意**不含**收尾的 `)`：COMMENT_RELAY_CONST 要插在 `)` **之前**（进 const 块内），
    #   若把 `)` 写进 old，new 里它就跑到了末尾 ⇒ old 不再是 new 的子串，lint 会（正确地）拦下。
    (T_RELAY,
     '\tdefMaxReservations = 512\n',
     '\tdefMaxReservations = 512\n' + COMMENT_RELAY_CONST),
    (T_RELAY,
     '\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n',
     '\t\tlibp2p.Muxer(yamux.ID, yamux.DefaultTransport),\n'
     '\t\t// ★ 批次8：identify 报出的 agentVersion（见 axLibp2pUserAgent 定义处）。\n'
     '\t\tlibp2p.UserAgent(axLibp2pUserAgent),\n'),
    # ── 抄件（第 7 处）：位置必须与 pl_libp2p.go 严格同序（Muxer 之后、Identity 之前）──
    # ★ old 取**块尾两行** `Identity(sk),` + `}`，新行插在 Identity **之前** ——
    #   这样 old 仍是 new 的连续子串（lint 的硬要求），且 Identity(sk) 全文件仅此一处。
    #   ★ 不要写成 "Muxer 行 + Identity 行" 两行连写：那样 new 中间多了注释 ⇒ old ⊄ new
    #   ⇒ lint 拦下（与 relaynode 常量那处同一个坑，登记过）。
    (T_COPY,
     '\t\tlibp2p.Identity(sk),\n\t}\n',
     '\t\t// ★ 批次8：与 pl_libp2p.go 同形 —— l2p_relay_e2e.py 的 check() 判\n'
     '\t\t//   "生产选项 ⊆ 抄件"，抄件漏项即翻红（实测过：漏 UserAgent 时它就红）。\n'
     '\t\tlibp2p.UserAgent(axLibp2pUserAgent),\n'
     '\t\tlibp2p.Identity(sk),\n\t}\n'),
]

# 回滚点：命名 `<树>.<相对路径>.<后缀>`（交付树外的统一回滚区，见 BAKDIR 约定）。
EDBACK = {
    T_AGENT: os.path.join(BAKDIR, "gopher_agent.src_gopher.transport_libp2p.go.bak-ua"),
    T_LST: os.path.join(BAKDIR, "gopher_listener_tcp.pl_libp2p.go.bak-ua"),
    T_RELAY: os.path.join(BAKDIR, "gopher_listener_tcp.relaynode.main.go.bak-ua"),
    T_COPY: os.path.join(BAKDIR, "gopher_agent.src_gopher.ax_relay_e2e_test.go.bak-ua"),
}

# 判据用的四个锚点：三处生产常量 + 测试文件里的独立锚点。
PROD_FILES = (T_AGENT, T_LST, T_RELAY)


def rd(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8") as f:
        f.write(s)


def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def back(p):
    """把 p 的当前内容存成回滚点（已存在则不覆盖 —— 第一次 apply 前的才是原件）。"""
    b = EDBACK[p]
    if not os.path.exists(b):
        shutil.copy2(p, b)
    return b


# ------------------------------------------------------- 取值纪律（反指纹）
def ua_violations(v):
    """返回 v 违反取值纪律的理由列表（空 = 合规）。"""
    bad = []
    low = v.lower()
    for s in ("adaptix", "gopher", "axc2", "command-line-arguments"):
        if s in low:
            bad.append("含自我标识 %r" % s)
    if "@" in v:
        bad.append("含 '@'（默认值的形状：模块名 + revision）")
    if re.search(r"[/@]\d", v):
        bad.append("'/' 或 '@' 后紧跟数字（版本号）⇒ 对端可反查已知漏洞")
    if not v:
        bad.append("空串")
    return bad


# ------------------------------------------------------------------ lint
def lint():
    """落盘前自检。**不是**可选的：一份错的锚点会把补丁打到别处，且照样"成功"。"""
    bad = []
    for path, old, new in TEXTS:
        if not os.path.exists(path):
            bad.append("找不到文件 %s" % path)
            continue
        if old not in new:
            bad.append("%s: new 里不含 old（替换会丢内容）锚点=%r" % (path, old[:40]))
        txt = rd(path)
        n = txt.count(old)
        if n == 0 and new not in txt:
            bad.append("%s: 锚点既不在文中、目标态也不在 ⇒ 源文件被外力改过：%r" % (path, old[:40]))
        if n > 1:
            bad.append("%s: 锚点出现 %d 次（必须唯一）：%r" % (path, n, old[:40]))
    # 同一文件的同一锚点不得出现两次（否则第二次 apply 会插重）
    seen = {}
    for path, old, _new in TEXTS:
        k = (path, old)
        seen[k] = seen.get(k, 0) + 1
    for (path, old), c in seen.items():
        if c > 1:
            bad.append("同一文件同一锚点被登记 %d 次：%s %r" % (c, path, old[:40]))
    # 取值纪律（落在脚本自身的常量上）
    for v in (UA,):
        for r in ua_violations(v):
            bad.append("中性值 %r %s" % (v, r))
    for e in EDBACK.values():
        if not e.startswith(BAKDIR + "/"):
            bad.append("回滚点 %s 不在 BAKDIR 内" % e)
    if bad:
        print("lint: FAIL")
        for b in bad:
            print("  - %s" % b)
        return False
    print("lint: ok（%d 处锚点唯一、new 含 old、回滚点均在 BAKDIR、中性值合规）" % len(TEXTS))
    return True


# ----------------------------------------------------------------- status
def state(path, old, new):
    txt = rd(path)
    if new in txt:
        return "done"
    if txt.count(old) == 1:
        return "todo"
    return "???"


def do_status():
    print("== 七处锚点状态（done=已是目标态 / todo=待打 / ???=锚点不唯一且非目标态）==")
    for path, old, new in TEXTS:
        tag = os.path.relpath(path, EXT)
        kind = "常量" if "UserAgent(axLibp2pUserAgent)" not in new else "opts"
        print("  [%-4s] %-46s %s" % (state(path, old, new), tag, kind))
    print("  测试文件 %s : %s" % (
        os.path.relpath(TESTFILE, EXT), "存在" if os.path.exists(TESTFILE) else "**缺失**"))


def do_check():
    ok = 0
    for path, old, new in TEXTS:
        s = state(path, old, new)
        if s == "done":
            ok += 1
        print("  %-4s %s" % (s, os.path.relpath(path, EXT)))
    print("check: done=%d todo=%d ???=%d" % (
        ok, sum(state(*t) == "todo" for t in TEXTS), sum(state(*t) == "???" for t in TEXTS)))
    print("  测试文件存在：%s" % os.path.exists(TESTFILE))
    return 0


# ------------------------------------------------------------------ apply
def do_apply():
    if not lint():
        return 1
    changed = []
    for path, old, new in TEXTS:
        txt = rd(path)
        if new in txt:
            print("  [skip] %s 已是目标态" % os.path.relpath(path, EXT))
            continue
        b = back(path)
        wr(path, txt.replace(old, new, 1))
        changed.append(path)
        print("  [ok  ] %s 已落盘（回滚点 %s）" % (os.path.relpath(path, EXT), os.path.basename(b)))
    # 落盘后回读：目标态必须在文中（不信 write 的返回值，只信读回来的字节）
    miss = [os.path.relpath(p, EXT) for p, _o, n in TEXTS if n not in rd(p)]
    if miss:
        print("apply: FAIL —— 回读时这些锚点未达目标态：%s" % ", ".join(miss))
        return 1
    print("apply: ok（%d 个文件改动，回读七处全部达目标态）" % len(set(changed)))
    for p in sorted(EDBACK):
        if os.path.exists(EDBACK[p]):
            print("  回滚点 %-64s %s" % (EDBACK[p], sha(EDBACK[p])[:16]))
    return 0


# ----------------------------------------------------------------- revert
# ★ REVERT 段与 APPLY 段写在一起（本项目约定：没有 git，回滚点 + 逆操作是唯一退路）。
def do_revert():
    n = 0
    for p, b in EDBACK.items():
        if not os.path.exists(b):
            print("  [skip] 无回滚点 %s" % b)
            continue
        shutil.copy2(b, p)
        n += 1
        print("  [ok  ] %s 已从 %s 还原" % (os.path.relpath(p, EXT), os.path.basename(b)))
    # 还原后回读：七处锚点必须全部回到 todo（即目标态不存在）
    left = [os.path.relpath(p, EXT) for p, _o, t in TEXTS if t in rd(p)]
    if left:
        print("revert: FAIL —— 还原后这些文件里仍能读到目标态：%s" % ", ".join(left))
        return 1
    print("revert: ok（%d 个文件还原，七处目标态均已消失）" % n)
    print("  ★ 测试文件不在回滚点内（它是**新增**文件）：交付前要去掉就 rm %s" % TESTFILE)
    return 0


# ----------------------------------------------------------------- verify
# ★ 与 l2p_deploy.sh:48-49 的 export **逐项一致**（实测：7 个变量）。
#   注意**没有** CGO_ENABLED —— 我记忆里"构建环境含 CGO_ENABLED"那条是错的，
#   批次8 读脚本原文更正（同族教训：环境/缺口清单是记忆，脚本里的是实现）。
GOENV = {
    "GOWORK": "off", "GOTOOLCHAIN": "local", "GOMAXPROCS": "1",
    "GOMODCACHE": "/root/go/pkg/mod", "GOCACHE": "/root/gp5cache",
    "TMPDIR": "/root/gp5probe/tmp", "GOTMPDIR": "/root/gp5probe/tmp",
    "PATH": "/usr/local/go/bin:/usr/bin:/bin",
}


def _code_only(txt):
    """剥掉 `//` 行注释后返回剩余文本（给计数型判据用）。

    ★ 只处理行注释：这三个文件（及本项目全树）都用 `//`，无 `/* */` 块注释。
      将来若引入块注释，本函数要一并处理，否则计数会虚高。
    """
    return "\n".join(l for l in txt.splitlines() if not l.lstrip().startswith("//"))


def _fmt_bad(path):
    """`gofmt -d` 想改的**原始行**数（`^-[^-]` ⇒ 排除 diff 的 `---` 头）。

    ★ 不用"文件尺寸"或"行数"当代理量 —— 与 DIAG 那条教训同族：代理量对目标不敏感。
    """
    r = subprocess.run(["/usr/local/go/bin/gofmt", "-d", path],
                       capture_output=True, text=True)
    return sum(1 for l in r.stdout.splitlines()
               if l.startswith("-") and not l.startswith("---"))


def _ua_anchor(path, pat):
    """从文件里抽出被 pat 捕获的中性值字面量。"""
    m = re.search(pat, rd(path))
    return m.group(1) if m else None


PAT_PROD = r'axLibp2pUserAgent\s*=\s*"([^"]*)"'
PAT_TEST = r'axUAWant\s*=\s*"([^"]*)"'


def do_verify():
    fails = []

    def ck(cond, okmsg, badmsg):
        if cond:
            print("  [ok  ] %s" % okmsg)
        else:
            print("  [FAIL] %s" % badmsg)
            fails.append(badmsg)

    print("== V1 七处锚点均为目标态 ==")
    for path, old, new in TEXTS:
        s = state(path, old, new)
        ck(s == "done", "%s" % os.path.relpath(path, EXT),
           "%s 状态=%s（期望 done）" % (os.path.relpath(path, EXT), s))

    print("== V2 四个锚点逐字节一致（三处生产常量 + 测试独立锚点）==")
    vals = {}
    for p in PROD_FILES:
        v = _ua_anchor(p, PAT_PROD)
        ck(v is not None, "%s → %r" % (os.path.relpath(p, EXT), v),
           "%s 里找不到 axLibp2pUserAgent 的定义" % os.path.relpath(p, EXT))
        vals[p] = v
    vt = _ua_anchor(TESTFILE, PAT_TEST) if os.path.exists(TESTFILE) else None
    ck(vt is not None, "ax_ua_test.go → axUAWant=%r" % vt,
       "测试文件缺失或没有独立锚点 axUAWant（判据会退化成重言式，见文件头第 11 形态）")
    vals[TESTFILE] = vt
    uniq = set(v for v in vals.values() if v is not None)
    ck(len(uniq) == 1 and None not in vals.values(),
       "四锚点全等：%r" % (uniq.pop() if len(uniq) == 1 else uniq),
       "四锚点不一致（漂移）：%s ⇒ 同一 C2 的不同部件会报出不同指纹" % (
           ", ".join("%s=%r" % (os.path.relpath(k, EXT), v) for k, v in sorted(vals.items()))))

    print("== V3 取值纪律（反指纹）==")
    val = _ua_anchor(T_AGENT, PAT_PROD)
    vs = ua_violations(val or "")
    ck(not vs, "中性值 %r 不含自我标识/版本号/@ 形状" % val,
       "中性值 %r 违规：%s" % (val, "; ".join(vs)))

    print("== V4 无遗漏：每个生产文件里 libp2p.New( 与 UserAgent( 次数相等 ==")
    # ★ 只数**代码行**：按全文数的话，将来任何人在注释里写一句 `libp2p.UserAgent(…)`
    #   都会让计数虚高 ⇒ "次数相等"凭空成立 ⇒ 假绿。三个文件的注释里现在都**没有**
    #   这个串，但判据的判别力不能建立在"当前恰好没有"上。
    if _code_only("// libp2p.UserAgent(x)\nlibp2p.New(o)\n").count("libp2p.UserAgent(") != 0:
        ck(False, "", "V4 的注释剥离器失效：注释行被算进了代码 ⇒ 后面的计数都不算数")
    for p in PROD_FILES:
        txt = _code_only(rd(p))
        n_new = len(re.findall(r"libp2p\.New\(", txt))
        n_ua = len(re.findall(r"libp2p\.UserAgent\(", txt))
        ck(n_new == n_ua and n_new > 0,
           "%s：libp2p.New=%d / UserAgent=%d" % (os.path.relpath(p, EXT), n_new, n_ua),
           "%s：libp2p.New=%d 但 UserAgent=%d ⇒ 有构造点漏设（默认值会自曝）"
           % (os.path.relpath(p, EXT), n_new, n_ua))

    print("== V7 抄件同构（**复用** l2p_relay_e2e.py 自己的抽取器与比较器）==")
    # ★ 这条判据的**本体在别处**：l2p_relay_e2e.py 的 check() 判 `生产选项 ⊆ 抄件`。生产加了
    #   UserAgent 而抄件没跟上 ⇒ 那边翻红（实测过）。这里**不重写第二份**抽取器 —— 直接
    #   import 那份脚本、用它自己的 opt_block/is_subseq：两处各写一份的话，对"什么算选项"
    #   的理解迟早漂移，那时谁红谁绿都说不清。★ 该脚本模块级只有常量与函数定义，副作用
    #   （prepare/selftest）都在 main() 里，import 安全。
    try:
        sys.path.insert(0, os.path.join(LST, "patches"))
        import l2p_relay_e2e as _e2e
    except Exception as e:
        ck(False, "", "V7 取不到 l2p_relay_e2e.py 的判据本体（%s）⇒ 同构性无从判定，"
                      "不能记成 PASS" % e)
    else:
        _pl, _ts = _e2e.opt_block(rd(T_LST)), _e2e.opt_block(rd(T_COPY))
        # 判别力自证：喂**已知漂移**的序列必须报红；再喂合法子序列不得误报
        #（后者防的是比较器方向写反 ⇒ 制造假红）。
        if _e2e.is_subseq(["A", "B"], ["A"]):
            ck(False, "", "V7 的比较器无判别力（把非子序列判成子序列）⇒ 后面的 PASS 不算数")
        elif not _e2e.is_subseq(["A"], ["A", "B"]):
            ck(False, "", "V7 的比较器方向反了（把子序列判成非子序列）⇒ 会制造假红")
        else:
            ck(_e2e.is_subseq(_pl, _ts),
               "抄件覆盖生产选项 %d/%d 项：%s" % (len(_pl), len(_ts), " ".join(_pl)),
               "生产的选项序列不是抄件的子序列 ⇒ 抄件已漂移（l2p_relay_e2e.py 会翻红）\n"
               "         生产 = %s\n         抄件 = %s" % (" ".join(_pl), " ".join(_ts)))

    print("== V6 未引入新的 gofmt 不合规 ==")
    # 度量：`gofmt -d` 想改的**原始行**数。只判"本批次没让它变多"——
    # transport_libp2p.go 有 4 行是**批次5 遗留**的（补丁前就不合规，与本批次同根因但
    # 不属本批次职责），修它需要在模板里硬编码别人补丁的中文注释，脆弱且越界 ⇒ 不修，登记。
    for p in sorted(EDBACK):
        cur, was = _fmt_bad(p), _fmt_bad(EDBACK[p])
        ck(cur <= was,
           "%s：不合规行 %d（补丁前 %d，未增加）" % (os.path.relpath(p, EXT), cur, was),
           "%s：不合规行从 %d 增到 %d ⇒ 本批次引入了新的格式问题（原因见模板前的纪律②）"
           % (os.path.relpath(p, EXT), was, cur))
    rr = subprocess.run(["/usr/local/go/bin/gofmt", "-l", TESTFILE],
                        capture_output=True, text=True)
    ck(rr.stdout.strip() == "",
       "ax_ua_test.go 完全合规（新增文件不该带格式债）",
       "ax_ua_test.go 不合规：%s" % rr.stdout.strip())

    print("== V5 动态判据：真起 host 对拨，读对端 identify 报来的 agentVersion ==")
    env = dict(os.environ)
    env.update(GOENV)
    # ★ -run 只跑这一条：libp2pHost 是包级单例，隔离执行顺序。
    r = subprocess.run(
        ["/usr/local/go/bin/go", "test", "-tags", "libp2p", "-count=1", "-v",
         "-run", "TestUAAgentVersion", "."],
        cwd=os.path.join(AGT, "src_gopher"), env=env,
        capture_output=True, text=True, timeout=600)
    out = r.stdout + r.stderr
    for line in out.splitlines():
        if "agentVersion" in line or "--- " in line or "FAIL" in line or "ok " in line:
            print("     | %s" % line.strip())
    ck(r.returncode == 0, "动态判据 PASS（含红侧物证：裸 host 的实测自曝值）",
       "动态判据 FAIL（rc=%d）—— 见上方 go test 输出" % r.returncode)
    ck("红侧" in out, "红侧物证已在输出中打印（证明观测通道有判别力）",
       "输出里没有红侧物证 ⇒ 无法确认观测通道有判别力")

    print()
    if fails:
        print("RESULT: FAIL（%d 项）" % len(fails))
        return 1
    print("RESULT: PASS（V1–V7 全绿）")
    return 0


def entry():
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    fn = {"status": do_status, "check": do_check, "apply": do_apply,
          "revert": do_revert, "verify": do_verify, "lint": lambda: 0 if lint() else 1}
    if mode not in fn:
        print("用法: l2p_uafix.py [status|check|lint|apply|revert|verify]")
        return 2
    try:
        return fn[mode]() or 0
    except Exception:
        import traceback
        traceback.print_exc()
        print("\nRESULT: FAIL（脚本异常，见上方 traceback）")
        return 1


if __name__ == "__main__":
    sys.exit(entry())
