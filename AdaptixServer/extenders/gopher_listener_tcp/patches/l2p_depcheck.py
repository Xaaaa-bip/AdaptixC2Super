#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""同进程插件的依赖版本一致性判据（审计 M6 / 事故 M1）。

## 它判什么

`/root/cc/run/adaptixserver` 与它加载的**每一个** `.so` 插件跑在同一个进程里，
Go 的 `plugin.Open` 会**逐包比对构建 ID** —— 只要同一个包路径在两边的版本不同，
就报：

    failed to open plugin extenders/.../x.so: plugin.Open("..."):
    plugin was built with a different version of package github.com/miekg/dns

★ 这条**真的发生过**（2026-09-16，`server.log:4273`）：根因**不是** teamserver 缺 dns
（它是有的），而是**另一个已加载插件带了另一个版本的 dns** ⇒ 光看"宿主有没有这个包"
永远查不出来，必须**横向比所有插件**。

## 为什么它是判据而不是装饰

`l2p_deploy.sh` 的头注释 ② 早就写下承诺"工具链/模块版本一致（plugin.Open 逐包比构建 ID）"，
而原 `pre()` **只比了工具链版本字符串**（`go version` 的第 2 个字段），模块版本一个都没比。
本脚本把那条承诺变成真的判据。

## ★★★ 本脚本**不能当门**（2026-09-16 实测推翻审计的修法建议）

审计 M6 的修法写的是"`pre()` 增加 `go version -m` 的 dep 逐项 diff"。**实测证明那会假红**：

| 事实 | 证据 |
|---|---|
| 静态 dep 矩阵报"冲突" | `beacon_listener_dns.so` dns=**v1.1.69** vs `listener_gopher_tcp.so` dns=**v1.1.61** |
| 但两者**确实都真的链接了 dns** | pclntab 里 `github.com/miekg/dns.` 符号各 **2** 处（对照 `notifier.so` = **0**）⇒ 不是"记录在 module graph 里但没编进去" |
| 可**它们此刻共存于活进程** | `/proc/<pid>/maps` 里两个 .so 都在；最近一次重启（22:43 之后）日志里**没有**新的加载失败 |

⇒ **"同名 module 版本不同" 既不是 "plugin.Open 会失败" 的充分条件、也不是必要条件。**
真正的检查发生在运行时，取决于**包的构建 ID**与**加载顺序**，静态清单里没有这个信息。
把这条当 `die` 用 ⇒ **每一次 install/restart 都会在 pre() 里死掉**，而且是**理由错误**地死掉
（脚本会说"依赖冲突"，而实际没有冲突）—— 这正是本项目归档里最忌讳的一类判据：
**红的理由必须是真理由**。

★ 而且它是**自伤**的：`pre()` 被 `do_build`/`do_install` 调用 ⇒ 一个假红判据会让**整条部署链永久不可用**。

### 因此本脚本的定位

**诊断工具**（默认不阻断，`exit 0`），用于回答"哪些 module 有多个版本、分别来自谁" ——
这在真出问题时是**最快的定位手段**（M1 事故就是这样查出来的）。
**真正的门在 `l2p_deploy.sh:do_restart`**：加载日志里出现
`plugin was built with a different version of package X` 就 `die` —— 那是**权威信号**，
且**已被实测红过**（`server.log:4273` / `:4346`，两次真实的加载失败）。

`--strict` 保留：只有在**明确知道本机不存在"同名不同版本但被接受"的插件**时才可用它当门。

## 判别力自证（`--selftest`）

"两份清单没冲突"这件事，在**比较器写错**时同样成立（比如按错字段分组、把 version 前缀截断）
—— 那是"两个空串也相等"的同族。所以本脚本自带一组**必然有冲突**的假清单：
比较器必须在它上报 FAIL。`--selftest` 红 ⇒ 比较器没判别力 ⇒ 后面所有的输出都不算数。

用法：
    l2p_depcheck.py             # 诊断：比 teamserver + run/extenders 下所有 .so（**不阻断**）
    l2p_depcheck.py --selftest  # 只跑比较器自检
    l2p_depcheck.py --verbose   # 顺带打印每个文件的 dep 条数
    l2p_depcheck.py --strict    # 冲突时 exit 1（见上：默认**不**这么做）
"""
import os
import re
import subprocess
import sys

GO = "/usr/local/go/bin/go"
SERVER = "/root/cc/run/adaptixserver"
EXT = "/root/cc/run/extenders"

# ★ 这两条取自真实事故（server.log:4273 与 4346）的原文字串，不是猜的。
#   注意日志里带 ANSI 颜色码（`\x1b[31m`），但这两个子串本身是连续的，可以整串匹配。
ERR_PAT = re.compile(r"plugin was built with a different version of package\s+(\S+)")


def parse_goversion(out):
    """`go version -m <file>` → {module_path: version_string}。

    行格式（制表符分隔）：
        \tdep\tgithub.com/miekg/dns\tv1.1.68\th1:...
        \t=>\t./third_party/go-libp2p\t(devel)\t          ← 修饰**上一个** dep（replace）
    ⇒ replace 必须并进上一个 dep 的版本串，否则 `third_party/go-libp2p`
      这个 replace 会被整个丢掉（而它正是 D3 那条隐藏构建前提）。
    """
    deps = {}
    last = None
    for line in out.splitlines():
        parts = [p for p in line.split("\t") if p != ""]
        if not parts:
            continue
        if parts[0] == "dep" and len(parts) >= 3:
            last = parts[1]
            deps[last] = parts[2]
        elif parts[0] == "=>" and last is not None and len(parts) >= 2:
            deps[last] = "%s => %s" % (deps[last], " ".join(parts[1:]))
    return deps


def read_deps(path):
    r = subprocess.run([GO, "version", "-m", path], capture_output=True, text=True)
    if r.returncode != 0:
        return None, (r.stderr or r.stdout).strip()
    return parse_goversion(r.stdout), None


def compare(named):
    """named = [(label, {mod: ver}), ...] → (冲突清单, 统计)。

    冲突 = 同一个 module path 在不同文件里出现了**不同**的 version 串。
    ★ 分组键是 **module path（含末尾 /vN）**，不是包路径的任意前缀 ——
      `github.com/x/y` 与 `github.com/x/y/v2` 是两个不同的 module，不算冲突。
    """
    seen = {}          # mod -> {ver: [labels]}
    for label, deps in named:
        for mod, ver in deps.items():
            seen.setdefault(mod, {}).setdefault(ver, []).append(label)
    conflicts = {m: v for m, v in seen.items() if len(v) > 1}
    return conflicts, len(seen)


# ---- 比较器自检：把"必然冲突"和"必然不冲突"两组喂进 compare() ----
SELFTEST_CONFLICT = [
    ("fake-teamserver", {"github.com/miekg/dns": "v1.1.68", "github.com/gin-gonic/gin": "v1.11.0"}),
    ("fake-pluginA",    {"github.com/miekg/dns": "v1.1.62", "github.com/gin-gonic/gin": "v1.11.0"}),
]
SELFTEST_CLEAN = [
    ("fake-teamserver", {"github.com/miekg/dns": "v1.1.68", "github.com/gin-gonic/gin": "v1.11.0"}),
    ("fake-pluginA",    {"github.com/miekg/dns": "v1.1.68", "github.com/gin-gonic/gin": "v1.11.0"}),
]


def selftest():
    bad, _ = compare(SELFTEST_CONFLICT)
    good, _ = compare(SELFTEST_CLEAN)
    ok = True
    if "github.com/miekg/dns" not in bad:
        print("  [FAIL] 比较器对**已知冲突**没报出来 ⇒ 它没有判别力，后面的 PASS 都不算数")
        ok = False
    else:
        print("  [ok  ] 已知冲突被报出：%s" % ", ".join(sorted(bad)))
    if good:
        print("  [FAIL] 比较器对**已知无冲突**的清单报了冲突 ⇒ 它会制造假红：%s" % ", ".join(sorted(good)))
        ok = False
    else:
        print("  [ok  ] 已知无冲突的清单未误报")
    # 反向样本：只差一个版本号就必须红（防止"永远返回空字典"的比较器蒙混）
    if len(bad) != 1:
        print("  [FAIL] 已知冲突样本只该报 1 个 module，实报 %d 个" % len(bad))
        ok = False
    return ok


def main():
    argv = sys.argv[1:]
    print("== 比较器自检 ==")
    if not selftest():
        print("RESULT: FAIL（比较器本身不可信）")
        return 1
    if "--selftest" in argv:
        print("RESULT: PASS（仅自检）")
        return 0

    targets = []
    if not os.path.exists(SERVER):
        print("  [BAD ] 找不到 %s" % SERVER)
        print("RESULT: FAIL")
        return 1
    targets.append(("teamserver", SERVER))
    sos = []
    for root, _dirs, files in os.walk(EXT):
        for f in sorted(files):
            if f.endswith(".so"):
                sos.append(os.path.join(root, f))
    for p in sos:
        targets.append((os.path.relpath(p, EXT), p))
    print("== 参与比对：teamserver + %d 个 .so ==" % len(sos))

    named, bad_read = [], []
    for label, path in targets:
        deps, err = read_deps(path)
        if deps is None:
            bad_read.append("%s: %s" % (label, err))
            continue
        named.append((label, deps))
        if "--verbose" in argv:
            print("  %-46s dep %d 条" % (label, len(deps)))
    if bad_read:
        print("  [BAD ] 这些文件读不出 dep 清单（观测通道失效，不是'没冲突'）：")
        for b in bad_read:
            print("         %s" % b)
        print("RESULT: FAIL")
        return 1
    # ★ 观测通道有效性：一份 dep 都没有 ⇒ "没冲突"是假绿
    if not named or all(len(d) == 0 for _l, d in named):
        print("  [BAD ] 所有文件的 dep 清单都是空的 ⇒ 解析通道失效，'无冲突'不成立")
        print("RESULT: FAIL")
        return 1

    conflicts, nmod = compare(named)
    print("== 比对 %d 个 module（跨 %d 个文件）==" % (nmod, len(named)))
    if conflicts:
        print("  [!   ] 同名 module 出现多个版本（**这不是 plugin.Open 失败的判据**，见文件头）：")
        for mod, vers in sorted(conflicts.items()):
            print("         %s" % mod)
            for ver, labels in sorted(vers.items()):
                print("           %-40s <- %s" % (ver, ", ".join(labels)))
        print("  [!   ] 权威判据在加载日志：`plugin was built with a different version of package X`")
        print("        （l2p_deploy.sh 的 do_restart 会 grep 它并 die）")
        if "--strict" in argv:
            print("RESULT: FAIL（--strict）")
            return 1
        print("RESULT: WARN（诊断模式，不阻断）")
        return 0
    print("  [ok  ] 所有同名 module 版本一致（这是一个**充分不必要**的好信号）")
    print("RESULT: PASS")
    return 0


def entry():
    """★ S12 修复：崩溃也必须留下 `RESULT:` 行（自动化框架只认它）。

    本脚本本来就靠 `RESULT:` 行表态，但 main() 里没有兜底 —— 一处意外
    （比如某个 .so 读到一半权限变了）就会让整次运行**一行 RESULT 都没有**，
    在汇总里与"没跑到"同形。
    """
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
