#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_build_schema.py -- 把 extenders/*/ax_config.axs 里的 BuildSchema 与插件 Go 源码对账。

为什么需要它
------------
Web 控制台（AdaptixWeb）不执行 .axs，它只拿 BuildSchema 返回的**纯数据**去渲染表单。
于是那张表成了「插件认什么参数」的**唯一**声明，却和真正的实现（pl_main.go 的
GenerateConfig 结构体、代码里实际比较过的字符串常量）隔了一层。手抄必然漂移：
qt 那份 combo 就漏了 "Stager"，而 pl_main.go 有 `Format == "Stager"` 的真分支。

这个脚本把两个方向都钉死：
  1. key 集合  ==  GenerateConfig 的 json tag 集合（必须完全相等）
  2. options   ⊇  Go 里实际比较过的字符串常量
     并且 options 里多出来的每一项，都必须在 ALLOWED_EXTRAS 里**带理由**登记
     （典型是 switch/if 的隐含 else：`Arch == "x86"` 只出现 "x86"，"x64" 是那个 else）

用法
----
    python check_build_schema.py                 # 扫本树
    python check_build_schema.py --extenders DIR # 扫别处的 extenders（如远端构建树）
    python check_build_schema.py --selftest      # 阳性对照：故意改坏，必须报错

★ 阳性对照不是可选项。「0 差异」和「解析器什么都没解析出来」在输出上长得一模一样，
  所以 --selftest 会对**真实解析出来的数据**做四种破坏，每一种都必须被抓到。
  另外每个 agent 都有 MIN_FIELDS 下限，字段表解析空了会直接报错，不会静默通过。
"""

import argparse
import os
import re
import sys

# ── options 里允许出现、但 Go 源码中查不到的取值 ────────────────────────────
# 每一项都必须写清理由。没有理由的「多出来的选项」= 操作员能选到一个插件不认的值。
ALLOWED_EXTRAS = {
    ("beacon", "arch"):
        'x64 是隐含的 else —— pl_main.go 只写 `Arch == "x86"`（:910 等 8 处），其余一律按 x64 走',
    ("beacon", "proxy_type"):
        'http 是隐含的 else —— pl_main.go:786 只写 `ProxyType == "https"`，其余按 http 走',
    ("beacon", "rotation_mode"):
        'sequential 是隐含的 else —— pl_main.go:756 只写 `RotationMode == "random"`',
}

# 字段数的下限：解析器要是把表读空了，这里先炸，不给「0 差异」的机会。
MIN_FIELDS = {"beacon": 32, "linux": 8}

# axs 里的 agent 目录名 -> 对外的 agent 名（就是 /api/agent/build/schema 的 agent 参数）
AGENT_DIRS = {"beacon_agent": "beacon", "linux_agent": "linux"}


# ── 解析 Go 侧 ─────────────────────────────────────────────────────────────

def go_config_tags(src):
    """取 GenerateConfig 的 (Go 字段名, json tag) 列表（按出现顺序）。

    ★ 必须成对返回：查「这个字段被比较过哪些字符串」要用 **Go 字段名**
      （`generateConfig.DnsMode`），而 schema 里的 key 是 **json tag**（`dns_mode`）。
      拿 json tag 去拼 `generateConfig.dns_mode` 会一条都匹配不到 —— 而
      「匹配不到」和「这个字段真没有取值域」在结果上完全一样，属于静默失效。
    """
    m = re.search(r"type\s+GenerateConfig\s+struct\s*\{", src)
    if not m:
        raise ValueError("找不到 type GenerateConfig struct")
    i = m.end()
    depth = 1
    body = []
    while i < len(src) and depth > 0:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        body.append(c)
        i += 1
    body = "".join(body)
    return [(g, t.split(",")[0])
            for g, t in re.findall(r'(\w+)\s+[^\s`]+\s+`json:"([^"]+)"', body)]


def _switch_cases(src, field):
    """`switch generateConfig.<field> { case "a": ... }` 里的 case 常量。"""
    out = []
    for m in re.finditer(r"switch\s+generateConfig\.%s\s*\{" % re.escape(field), src):
        i = m.end()
        depth = 1
        while i < len(src) and depth > 0:
            c = src[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = src[m.end():i]
        # 只取深度 1 的 case（嵌套 switch 的 case 归它自己那个字段管）
        depth = 0
        for line in block.split("\n"):
            if depth == 0:
                cm = re.match(r'\s*case\s+"([^"]*)"', line)
                if cm:
                    out.append(cm.group(1))
            depth += line.count("{") - line.count("}")
    return out


def _equality_literals(src, field):
    """`generateConfig.<field> == "a"` / `!= "a"` 里的常量。"""
    pat = r'generateConfig\.%s\s*(?:==|!=)\s*"([^"]*)"' % re.escape(field)
    return re.findall(pat, src)


def go_field_literals(go_src, field):
    """这个字段在 Go 里被实际比较过的全部字符串常量（去重、保序）。"""
    seen, out = set(), []
    for lit in _equality_literals(go_src, field) + _switch_cases(go_src, field):
        if lit not in seen:
            seen.add(lit)
            out.append(lit)
    return out


# ── 解析 .axs 侧 ───────────────────────────────────────────────────────────

def axs_schema_fields(src):
    """把 BuildSchema 里 fields:[...] 的每个顶层对象抠出来，返回 dict 列表。"""
    fm = re.search(r"fields\s*:\s*\[", src)
    if not fm:
        raise ValueError("BuildSchema 里找不到 fields: [")
    i = fm.end()
    depth, start = 1, None
    chunks = []
    in_str, esc = False, False
    while i < len(src):
        c = src[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c in "[{":
            if c == "{" and depth == 1:
                start = i
            depth += 1
        elif c in "]}":
            depth -= 1
            if c == "}" and depth == 1 and start is not None:
                chunks.append(src[start:i + 1])
                start = None
            elif depth == 0:
                break
        i += 1

    fields = []
    for ch in chunks:
        km = re.search(r'\bkey\s*:\s*"([^"]+)"', ch)
        if not km:
            continue
        om = re.search(r"\boptions\s*:\s*\[([^\]]*)\]", ch)
        opts = []
        if om:
            opts = [s for s in re.findall(r'"([^"]*)"', om.group(1))]
        fields.append({"key": km.group(1), "options": opts, "raw": ch})
    return fields


# ── 对账 ───────────────────────────────────────────────────────────────────

def check_agent(agent, axs_path, go_path):
    """返回 (problems, info)。problems 为空即通过。"""
    problems = []
    axs_src = open(axs_path, "rb").read().decode("utf-8")
    go_src = open(go_path, "rb").read().decode("utf-8")

    pairs = go_config_tags(go_src)
    gofield = {t: g for g, t in pairs}      # json tag -> Go 字段名
    tags = [t for _, t in pairs]
    fields = axs_schema_fields(axs_src)
    keys = [f["key"] for f in fields]

    info = {"tags": tags, "fields": fields}

    floor = MIN_FIELDS.get(agent)
    if floor is not None and len(fields) < floor:
        problems.append(
            "字段表只解析出 %d 项（%s 的下限是 %d）—— 要么真少了，要么解析器瞎了，"
            "先当错误处理" % (len(fields), agent, floor))
        return problems, info

    # ① key 集合必须完全相等
    for k in tags:
        if k not in keys:
            problems.append("GenerateConfig 有 json tag %r，BuildSchema 里没有这个 key" % k)
    for k in keys:
        if k not in tags:
            problems.append("BuildSchema 声明了 key %r，GenerateConfig 里没有这个 json tag" % k)
    dup = [k for k in keys if keys.count(k) > 1]
    for k in sorted(set(dup)):
        problems.append("BuildSchema 里 key %r 重复出现 %d 次" % (k, keys.count(k)))

    # ② options ⊇ Go 里实际比较过的常量；多出来的必须带理由登记
    for f in fields:
        if not f["options"]:
            continue
        if f["key"] not in gofield:
            continue          # 上面 ① 已经报过了，别重复报
        lits = go_field_literals(go_src, gofield[f["key"]])
        for lit in lits:
            if lit not in f["options"]:
                problems.append(
                    "%s.options 缺了 Go 里实际比较过的取值 %r（插件认它，操作员却选不到）"
                    % (f["key"], lit))
        if not lits:
            continue
        for opt in f["options"]:
            if opt in lits:
                continue
            why = ALLOWED_EXTRAS.get((agent, f["key"]))
            if why is None:
                problems.append(
                    "%s.options 里的 %r 在 Go 里查不到对应常量；若它是隐含的 else，"
                    "请连理由一起登记进 ALLOWED_EXTRAS[('%s', '%s')]"
                    % (f["key"], opt, agent, f["key"]))

    return problems, info


# ── 主流程 ─────────────────────────────────────────────────────────────────

def discover(extenders):
    found = []
    for d in sorted(os.listdir(extenders)):
        axs = os.path.join(extenders, d, "ax_config.axs")
        go = os.path.join(extenders, d, "pl_main.go")
        if not (os.path.isfile(axs) and os.path.isfile(go)):
            continue
        if "function BuildSchema" not in open(axs, "rb").read().decode("utf-8", "replace"):
            continue
        found.append((AGENT_DIRS.get(d, d), axs, go))
    return found


def selftest(extenders):
    """阳性对照：对真实数据做四种破坏，每一种都必须被 check_agent 抓到。"""
    targets = discover(extenders)
    if not targets:
        print("SELFTEST FAIL: 一个实现了 BuildSchema 的 agent 都没有，无从对照")
        return 1
    agent, axs_path, go_path = targets[0]
    axs_src = open(axs_path, "rb").read().decode("utf-8")
    go_src = open(go_path, "rb").read().decode("utf-8")
    fields = axs_schema_fields(axs_src)
    tags = go_config_tags(go_src)

    # 先证明解析器在这份真数据上是有效的
    if not tags or not fields:
        print("SELFTEST FAIL: 解析器在真实数据上返回了空表（tags=%d fields=%d）"
              % (len(tags), len(fields)))
        return 1

    cases = []

    # (a) 从 schema 里删掉一个 key
    victim = fields[0]["key"]
    mutated = axs_src.replace('key: "%s"' % victim, 'key: "__gone__"', 1)
    cases.append(("删掉 schema 的 key %r" % victim,
                  check_agent(agent, _tmp(mutated), _tmp(go_src))[0]))

    # (b) 从 schema 的 options 里删掉一个 Go 认的取值
    tgt = None
    for f in fields:
        lits = go_field_literals(go_src, f["key"])
        if lits and lits[0] in f["options"]:
            tgt = (f, lits[0])
            break
    if tgt is None:
        print("SELFTEST FAIL: 找不到「有 options 且与 Go 对得上」的字段，对照无效")
        return 1
    f, lit = tgt
    mutated = axs_src.replace('"%s", ' % lit, "", 1).replace('"%s",' % lit, "", 1)
    cases.append(("从 %s.options 里删掉 Go 认的 %r" % (f["key"], lit),
                  check_agent(agent, _tmp(mutated), _tmp(go_src))[0]))

    # (c) 往 schema 的 options 里塞一个 Go 不认的取值
    mutated = axs_src.replace('"%s"' % f["options"][0], '"%s", "__bogus__"' % f["options"][0], 1)
    cases.append(("往 %s.options 里塞 __bogus__" % f["key"],
                  check_agent(agent, _tmp(mutated), _tmp(go_src))[0]))

    # (d) 从 Go 的 GenerateConfig 里删掉一个 tag
    mutated = go_src.replace('json:"%s"' % tags[0], 'json:"__mutated__"', 1)
    cases.append(("把 GenerateConfig 的 json tag %r 改名" % tags[0],
                  check_agent(agent, _tmp(axs_src), _tmp(mutated))[0]))

    bad = 0
    for name, probs in cases:
        if probs:
            print("SELFTEST ok   : %-52s -> 抓到 %d 条" % (name, len(probs)))
        else:
            print("SELFTEST FAIL : %-52s -> 一条都没抓到（判据是瞎的）" % name)
            bad += 1
    return 1 if bad else 0


_TMPDIR = None


def _tmp(text):
    global _TMPDIR
    if _TMPDIR is None:
        import tempfile
        _TMPDIR = tempfile.mkdtemp(prefix="axsbs-")
    p = os.path.join(_TMPDIR, "m%d.axs" % len(os.listdir(_TMPDIR)))
    open(p, "wb").write(text.encode("utf-8"))
    return p


def main():
    ap = argparse.ArgumentParser()
    default_ext = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "AdaptixServer", "extenders")
    ap.add_argument("--extenders", default=default_ext,
                    help="extenders 目录（默认：本脚本所在树的 AdaptixServer/extenders）")
    ap.add_argument("--selftest", action="store_true", help="阳性对照：故意改坏，必须报错")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.extenders)

    if not os.path.isdir(args.extenders):
        print("extenders 目录不存在：%s" % args.extenders)
        return 2

    targets = discover(args.extenders)
    if not targets:
        print("没有找到任何实现了 BuildSchema 的 agent —— 这多半是错的，不是好消息")
        return 1

    total = 0
    for agent, axs_path, go_path in targets:
        problems, info = check_agent(agent, axs_path, go_path)
        status = "OK  " if not problems else "FAIL"
        print("%s %-8s %2d fields vs %2d json tags   (%s)"
              % (status, agent, len(info["fields"]), len(info["tags"]),
                 os.path.relpath(axs_path, args.extenders)))
        for p in problems:
            print("       - %s" % p)
            total += 1

    # 顺带报一下没迁移的 agent，免得它们被当成"没问题"
    migrated = {a for a, _, _ in targets}
    pending = []
    for d in sorted(os.listdir(args.extenders)):
        go = os.path.join(args.extenders, d, "pl_main.go")
        if os.path.isfile(go) and AGENT_DIRS.get(d, d) not in migrated:
            if re.search(r"type\s+GenerateConfig\s+struct", open(go, "rb").read().decode("utf-8", "replace")):
                pending.append(AGENT_DIRS.get(d, d))
    if pending:
        print("未实现 BuildSchema（Web 走内置回退界面，不是错误）：%s" % ", ".join(pending))

    print("---- %d 个 agent，%d 条差异" % (len(targets), total))
    return 1 if total else 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())
