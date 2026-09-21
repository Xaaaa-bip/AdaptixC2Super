#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次5 relay（C2 IP 隐藏）的**端到端门** —— 跑真代码，不是跑探针。

## 覆盖的两棵树

| 树 | 判据本体 | 判什么 |
|---|---|---|
| agent | `gopher_agent/src_gopher/ax_relay_e2e_test.go`（`//go:build libp2p`） | 真 `dialLibp2p` 能不能经 relay 开流；限额红/绿；identify 泄不泄漏 C2 真 IP；relay 停掉是否真断 |
| relay 节点 | `gopher_listener_tcp/relaynode/relaynode_test.go` | 交付的 relay 节点能不能被预约、限额是否放宽、身份是否持久化 |

两处都在**同一个包里**直呼真函数（`libp2pHost`/`dialLibp2p`；`newRelayHost`/`newResources`/
`loadOrCreateKey`），测试进程内起真的 circuit v2 relay 与 C2 host —— 不需要 teamserver，
也不会碰到运行态。

## 九条判据

agent 侧（6）：
  TestRelayE2EDialAndEcho                真 dialLibp2p 在 circuit 上开流+回显
  TestRelayLimitDefaultTruncates         默认限额必须掐断（对照组）
  TestRelayLimitRaisedPasses             放宽限额必须完整回显
  TestRelayIdentifyLeaksWithoutFactory   不装 factory 时**能**看到 C2 真 IP（对照组）
  TestRelayIdentifyHiddenWithFactory     装了 factory 就看不到 C2 真 IP
  TestRelayDownFails                     relay 停 ⇒ 拨不上（交付形态的阴性）
relay 节点（3）：
  TestRelayNodeAcceptsReservation        真节点能接受预约，且限额协商一致
  TestRelayNodeLimitsAreRaised           约定限额高于上游默认
  TestRelayNodeIdentityPersists          身份持久化 + 0600

★ "对照组"三条不是凑数：没有它们，"看不到 / 拨不上 / 没说错"在**观测通道本身失效**时
同样成立 —— 那是本项目归档的「两个空串也相等」假绿第五形态。

## 判别力自证（`selftest`）

判据绿了不等于判据有判别力。`selftest` 在**副本**上逐个施加破坏样本，每个都必须让
**指定**判据从 PASS 翻红 —— 翻不动就说明那条判据是装饰：

1. agent：`network.WithAllowLimitedConn(ctx, "gopher-c2")` → 裸 `ctx`
   （连带把 `core/network` 变 blank import，否则是编译错而不是判据红）
2. agent：删掉 opts 里的 `libp2p.EnableRelay(),`
3. agent 测试：`if hideC2 {` → `if false {`（C2 永不装 AddrsFactory）
4. relaynode：删掉 `libp2p.ForceReachabilityPublic(),` —— 节点仍会监听、不报错，
   但一台预约都不接受（本节点最贵的那个坑）
5. relaynode：约定数据限额改回上游默认 `1<<17`

★ 破坏样本一律施加在 `$SCRATCH` 的副本上，**真树只读**。
★ 命中 0 处一律判失败：改了 0 处的"红"和"绿"都不算数。

用法：
    l2p_relay_e2e.py            # = verify（check + run + selftest）
    l2p_relay_e2e.py check      # 静态：文件在不在、选项序列与真代码同构
    l2p_relay_e2e.py run        # 只跑九条判据
    l2p_relay_e2e.py selftest   # 只跑破坏样本自证
"""
import os
import re
import shutil
import subprocess
import sys

GO = "/usr/local/go/bin/go"
LST = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp"
AGT = "/root/cc/AdaptixServer/extenders/gopher_agent/src_gopher"
SCRATCH = "/root/gp5probe/relay-e2e"
WORK = SCRATCH + "/src_gopher"      # agent 副本
LWORK = SCRATCH + "/lst"            # listener/relaynode 副本
TMPD = "/root/gp5probe/tmp"

F_AGENT = "transport_libp2p.go"
F_TEST = "ax_relay_e2e_test.go"
F_NODE = "relaynode/main.go"
F_LST = LST + "/pl_libp2p.go"

ENV = dict(os.environ)
ENV.update({
    "GOWORK": "off", "GOTOOLCHAIN": "local", "GOMAXPROCS": "1",
    "GOMODCACHE": "/root/go/pkg/mod", "GOCACHE": "/root/gp5cache",
    "TMPDIR": TMPD, "GOTMPDIR": TMPD, "CGO_ENABLED": "0",
})

AGENT_TESTS = [
    ("TestRelayE2EDialAndEcho", "真 dialLibp2p 在 circuit 上开流+回显"),
    ("TestRelayLimitDefaultTruncates", "默认限额必须掐断（对照组）"),
    ("TestRelayLimitRaisedPasses", "放宽限额必须完整回显"),
    ("TestRelayIdentifyLeaksWithoutFactory", "不装 factory 时能看到 C2 真 IP（对照组）"),
    ("TestRelayIdentifyHiddenWithFactory", "装了 factory 就看不到 C2 真 IP"),
    ("TestRelayDownFails", "relay 停 ⇒ 拨不上（交付形态的阴性）"),
]
NODE_TESTS = [
    ("TestRelayNodeAcceptsReservation", "真 relay 节点能接受预约，且限额协商一致"),
    ("TestRelayNodeLimitsAreRaised", "约定限额高于上游默认"),
    ("TestRelayNodeIdentityPersists", "身份持久化 + 0600"),
]


# ---------------- 破坏样本助手：每条返回 (新文本, 命中数) ----------------

def _lit(old, new):
    def f(t):
        return t.replace(old, new), t.count(old)
    return f


def _re(pat, new):
    def f(t):
        return re.subn(pat, new, t)
    return f


def _del_line(needle):
    def f(t):
        out, n = [], 0
        for ln in t.split("\n"):
            if ln.strip() == needle:
                n += 1
                continue
            out.append(ln)
        return "\n".join(out), n
    return f


def _two(fa, fb):
    """先施加 fa 再施加 fb，命中数相乘（任一为 0 即整体不成立）。"""
    def f(t):
        t1, n1 = fa(t)
        t2, n2 = fb(t1)
        return t2, n1 * n2
    return f


IMPORT_NET = '\t"github.com/libp2p/go-libp2p/core/network"'
# (说明, 树, 文件, 施加函数, 期望翻红的判据)
BREAKS = [
    ("agent：WithAllowLimitedConn 改回裸 ctx（并把 core/network 变 blank import，"
     "否则是编译错而非判据红）", "agent", F_AGENT,
     _two(_lit('network.WithAllowLimitedConn(ctx, "gopher-c2")', 'ctx'),
          _lit(IMPORT_NET, '\t_ ' + IMPORT_NET.strip())),
     "TestRelayE2EDialAndEcho"),
    ("agent：删掉 opts 里的 libp2p.EnableRelay(),", "agent", F_AGENT,
     _del_line("libp2p.EnableRelay(),"), "TestRelayE2EDialAndEcho"),
    ("agent 测试：if hideC2 → if false（C2 永不装 AddrsFactory）", "agent", F_TEST,
     _lit("if hideC2 {", "if false {"), "TestRelayIdentifyHiddenWithFactory"),
    ("relaynode：删掉 ForceReachabilityPublic()（节点照常监听，但一台预约都不接受）",
     "node", F_NODE, _del_line("libp2p.ForceReachabilityPublic(),"),
     "TestRelayNodeAcceptsReservation"),
    ("relaynode：约定数据限额改回上游默认 1<<17", "node", F_NODE,
     _re(r"defLimitData\s*=\s*1 << 30", "defLimitData = 1 << 17"),
     "TestRelayNodeLimitsAreRaised"),
]


# ---------------- 基础 ----------------

def say(tag, msg):
    print("  [%s] %s" % (tag, msg))


def prepare():
    """把真树复制到 scratch（**真树只读**）。"""
    for d in (AGT, LST):
        if not os.path.isdir(d):
            raise RuntimeError("找不到真树 %s" % d)
    for d in (WORK, LWORK):
        if os.path.exists(d):
            shutil.rmtree(d)
    os.makedirs(SCRATCH, exist_ok=True)
    os.makedirs(TMPD, exist_ok=True)
    r = subprocess.run(["cp", "-a", AGT, WORK], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("复制 agent 树失败: %s" % (r.stderr or r.stdout))
    # relaynode 只需要 module 骨架（go.mod 的 replace 指向 ./third_party），
    # 不必把 dist/（29 MB 产物）与 patches/ 也拖过来。
    os.makedirs(LWORK, exist_ok=True)
    for item in ("go.mod", "go.sum", "third_party", "relaynode"):
        src = os.path.join(LST, item)
        if not os.path.exists(src):
            raise RuntimeError("listener 树缺 %s（go.mod 的 replace 靠它）" % item)
        r = subprocess.run(["cp", "-a", src, LWORK + "/"], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("复制 %s 失败: %s" % (item, r.stderr or r.stdout))


RES_RE = re.compile(r"^\s*--- (PASS|FAIL|SKIP): (\S+)", re.M)


def parse(out):
    return {m.group(2): m.group(1) for m in RES_RE.finditer(out)}


def gotest(cwd, run, target="."):
    return subprocess.run(
        [GO, "test", "-tags", "libp2p", "-count=1", "-v", "-run", run, target],
        cwd=cwd, env=ENV, capture_output=True, text=True, timeout=900)


def judge(st, tests):
    """→ (全部合格?, rows)。★ SKIP 与"没跑到"都判**不合格**：前者是环境不成立
    （观测通道无效），后者在汇总里与"没跑"同形 —— 本项目归档过
    「读批次只看 RESULT 会数出 0 项完成」。"""
    rows, ok = [], True
    for name, desc in tests:
        got = st.get(name)
        if got == "PASS":
            rows.append((name, "PASS", desc))
        elif got is None:
            rows.append((name, "MISS", "测试没跑到（编译失败？被 -run 过滤掉？）"))
            ok = False
        else:
            rows.append((name, got, "不合格：%s" % desc))
            ok = False
    return ok, rows


def report(rows, total):
    for name, status, desc in rows:
        say("ok  " if status == "PASS" else "FAIL",
            "%-42s %s" % (name, desc if status == "PASS" else "%s [%s]" % (desc, status)))
    say("info", "通过 %d/%d 项" % (sum(1 for _n, s, _d in rows if s == "PASS"), total))


# ---------------- check：静态判据（含同构判据）----------------

OPT_ANCHOR = "opts := []libp2p.Option{"
# ★ 必须同时匹配**函数型**（`libp2p.EnableRelay()`）与**值型**（`libp2p.NoTransports,`）
#   两种选项：实测过 —— 只写 `libp2p\.(\w+)\(` 会把 NoTransports 整个漏掉，而
#   "有序子序列"判据在**漏项**时依然成立 ⇒ 比对恒真、观测通道却是坏的。
OPT_RE = re.compile(r"libp2p\.(\w+)\s*[,(]")


def opt_block(text):
    i = text.find(OPT_ANCHOR)
    if i < 0:
        return None
    j = text.find("\n\t}", i)
    if j < 0:
        return None
    # 从锚点**之后**开始：锚点自己含 `libp2p.Option{`，否则会把 Option 也抽进来。
    return OPT_RE.findall(text[i + len(OPT_ANCHOR):j])


def is_subseq(a, b):
    it = iter(b)
    return all(x in it for x in a)


def order_ok(seq, first, second):
    if first not in seq or second not in seq:
        return False
    return seq.index(first) < seq.index(second)


def selftest_cmp():
    """比较器自检：错序序列必须被判定为"不同构"。"""
    a = ["ListenAddrStrings", "NoTransports", "EnableRelay"]
    bad = ["ListenAddrStrings", "EnableRelay", "NoTransports"]
    ok = True
    if not is_subseq(a, a):
        say("FAIL", "比较器把**自身**判成不同构 ⇒ 无判别力")
        ok = False
    if is_subseq(a, bad):
        say("FAIL", "比较器对**已知错序**返回了同构 ⇒ 无判别力，后面的 PASS 不算数")
        ok = False
    if order_ok(bad, "NoTransports", "EnableRelay"):
        say("FAIL", "顺序判据对已知错序样本返回 OK ⇒ 无判别力")
        ok = False
    if not order_ok(a, "NoTransports", "EnableRelay"):
        say("FAIL", "顺序判据把已知正确样本判成错序 ⇒ 会制造假红")
        ok = False
    if ok:
        say("ok", "同构比较器自检通过（错序必红、正序不误报）")
    return ok


def check():
    ok = True
    if not os.path.isfile(AGT + "/" + F_TEST):
        say("FAIL", "真树没有 %s（agent 侧判据本体不在）" % F_TEST)
        return False
    test_txt = open(AGT + "/" + F_TEST, encoding="utf-8").read()
    for name, _d in AGENT_TESTS:
        if ("func %s(" % name) not in test_txt:
            say("FAIL", "判据本体里没有 %s" % name)
            ok = False
    node_txt = open(LST + "/" + F_NODE, encoding="utf-8").read()
    node_test = LST + "/relaynode/relaynode_test.go"
    if not os.path.isfile(node_test):
        say("FAIL", "relaynode 没有测试本体 %s" % node_test)
        ok = False
    else:
        nt = open(node_test, encoding="utf-8").read()
        for name, _d in NODE_TESTS:
            if ("func %s(" % name) not in nt:
                say("FAIL", "relaynode 测试里没有 %s" % name)
                ok = False
    if ok:
        say("ok", "两棵树的判据本体齐全（agent %d 条 / relaynode %d 条）"
            % (len(AGENT_TESTS), len(NODE_TESTS)))

    # ★ 点名核对 relaynode 的两个要害没被删掉（它们是"照常监听但毫无作用"的成因）。
    for needle, why in (("libp2p.ForceReachabilityPublic()", "没有它 relaysvc 永不建 relay"),
                        ("libp2p.EnableRelayService(", "没有它根本不是 relay"),
                        ("libp2p.Identity(sk)", "没有它 peer id 每次都变")):
        if needle not in node_txt:
            say("FAIL", "relaynode/main.go 缺 %s（%s）" % (needle, why))
            ok = False
    if ok:
        say("ok", "relaynode/main.go 三处要害齐全")

    if not selftest_cmp():
        ok = False

    lst_txt = open(F_LST, encoding="utf-8").read()
    pl, ts = opt_block(lst_txt), opt_block(test_txt)
    if not pl or not ts:
        say("FAIL", "选项块抽不出来（pl=%s test=%s）—— 观测通道失效，"
                    "'两边一致'不成立" % (pl, ts))
        return False
    say("info", "listener opts: %s" % ", ".join(pl))
    say("info", "测试内 C2 opts: %s" % ", ".join(ts))
    if not is_subseq(pl, ts):
        say("FAIL", "测试内 C2 的选项序列与 pl_libp2p.go 不同构（不再是它的有序子序列）"
                    "⇒ 抄件已漂移，测试测的不是交付形态")
        ok = False
    else:
        say("ok", "测试内 C2 的选项序列是 pl_libp2p.go 的有序子序列（%d/%d 项）"
            % (len(pl), len(ts)))
    # ★ 先点名核对抽取器**抓没抓到**这几个选项，再谈顺序 —— 两者的红是**不同**的理由，
    #   混在一起会报出"顺序可疑"而真实病因是"漏项"（红的理由必须是真理由）。
    for label, seq in (("listener", pl), ("测试内 C2", ts)):
        missing = [x for x in ("NoTransports", "EnableRelay", "Transport") if x not in seq]
        if missing:
            say("FAIL", "%s 的选项序列里没有 %s ⇒ 抽取器漏项，"
                        "'两边同构'是假绿" % (label, "、".join(missing)))
            ok = False
        elif not order_ok(seq, "NoTransports", "EnableRelay"):
            say("FAIL", "%s 的 EnableRelay 排到了 NoTransports 之前 —— options.go:436-438 "
                        "会把 relay 一起关掉：%s" % (label, seq))
            ok = False
        else:
            say("ok", "%s：NoTransports 在前、EnableRelay 在后" % label)
    return ok


# ---------------- run ----------------

def run():
    prepare()
    ok = True
    # ★ relaynode 的 target 必须是 `./relaynode`：LWORK 根目录**没有** .go 文件
    #   （只复制了 module 骨架），`go test .` 在那里会以"没有 Go 文件"失败。
    for cwd, target, tests, label in ((WORK, ".", AGENT_TESTS, "agent 侧（真 dialLibp2p）"),
                                      (LWORK, "./relaynode", NODE_TESTS, "relay 节点（交付物）")):
        print("-- %s" % label)
        r = gotest(cwd, "TestRelay|TestRelayNode", target)
        st = parse(r.stdout + r.stderr)
        good, rows = judge(st, tests)
        report(rows, len(tests))
        if not st:
            say("FAIL", "一条测试结果都没解析到 ⇒ 多半是编译失败，尾部输出：")
            print("\n".join((r.stdout + r.stderr).splitlines()[-12:]))
        ok = good and ok
    return ok


# ---------------- selftest ----------------

def selftest():
    if not run():
        say("FAIL", "基线就没全绿 ⇒ 无法判断破坏样本是否命中，自证不成立")
        return False
    print("== 破坏样本（每个都必须让指定判据翻红）==")
    all_ok = True
    for desc, tree, fname, fn, expect in BREAKS:
        prepare()
        path = os.path.join(WORK if tree == "agent" else LWORK, fname)
        txt = open(path, encoding="utf-8").read()
        new, hits = fn(txt)
        if hits < 1:
            say("FAIL", "破坏「%s」没命中任何锚点（改了 0 处）⇒ 这次的红/绿都不算数" % desc)
            all_ok = False
            continue
        open(path, "w", encoding="utf-8").write(new)
        cwd, target = (WORK, ".") if tree == "agent" else (LWORK, "./relaynode")
        r = gotest(cwd, "TestRelay|TestRelayNode", target)
        got = parse(r.stdout + r.stderr).get(expect)
        if got in ("FAIL", "SKIP"):
            say("ok", "破坏「%s」（命中 %d 处）⇒ 判据 %s 变红" % (desc, hits, expect))
        else:
            say("FAIL", "破坏「%s」（命中 %d 处）后判据 %s 仍是 %s ⇒ 该判据没有判别力"
                        % (desc, hits, expect, got))
            all_ok = False
    prepare()
    return all_ok


# ---------------- entry ----------------

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "verify"
    if mode not in ("check", "run", "selftest", "verify"):
        print("用法: %s [check|run|selftest|verify]" % sys.argv[0])
        print("RESULT: FAIL（未知模式）")
        return 2
    ok = True
    if mode in ("check", "verify"):
        print("== check：静态判据 + 同构判据 ==")
        ok = check() and ok
    if mode in ("run", "verify"):
        print("== run：跑真代码的 %d 条判据 ==" % (len(AGENT_TESTS) + len(NODE_TESTS)))
        ok = run() and ok
    if mode in ("selftest", "verify"):
        print("== selftest：判别力自证 ==")
        ok = selftest() and ok
    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def entry():
    """S12 纪律：崩溃也必须留下 `RESULT:` 行（自动化框架只认它）。"""
    try:
        return main()
    except KeyboardInterrupt:
        print("\nRESULT: FAIL（被中断）")
        return 130
    except Exception:
        import traceback
        traceback.print_exc()
        print("\nRESULT: FAIL（脚本异常，见上方 traceback）")
        return 1


if __name__ == "__main__":
    sys.exit(entry())
