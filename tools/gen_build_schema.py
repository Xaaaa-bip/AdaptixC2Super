#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_build_schema.py -- 从插件的 Go 源码生成 .axs 里的 BuildSchema 函数。

真值源只有一个：**插件的 Go 源码**。这张表里每一项都从它能指出的地方抽出来：

    有哪些参数    <-  GenerateConfig 结构体的 json tag
    控件类型      <-  Go 的类型（string -> 输入框 / int -> 数字 / bool -> 勾选框）
    选项列表      <-  代码里**实际比较过**的字符串常量（== / != / switch case）
    标签 分组 提示 <-  字段上的 ax:"..." tag，由**加这个字段的人**在同一行写下

回退链（没写 ax tag 的插件照样能用，只是标签是英文）：
    标签 = ax 的 label  ->  key 美化（proxy_port -> "Proxy Port"）  ->  原样显示 key
    控件 = ax 的 type   ->  Go 类型推导
    选项 = Go 比较常量 ∪ ax 的 extra（隐含 else 那种 Go 里没比较过的值）

为什么是「生成」而不是「手写 + 校验」：手写的表加上一个校验脚本，本质仍是硬编码，
脚本只是一个人主动跑才生效的警报。生成则让插件新增字段**必然**出现在界面上 ——
失败方向是「显示出来但标签丑」，不是「静默消失」。

用法
----
    python gen_build_schema.py                      # 看会生成什么（计数与告警），不写盘
    python gen_build_schema.py --agent beacon --print   # 把要写的 JS 原文打出来
    python gen_build_schema.py --agent beacon       # 只看一个 agent
    python gen_build_schema.py --write              # 真正写进 ax_config.axs
    python gen_build_schema.py --selftest           # 阳性对照：证明这条链是活的

★ --selftest 不是可选项。「生成正确」和「生成器什么都没生成」在输出上长得一样，
  所以它往 Go 源码里塞一个假字段、改一个真常量，断言两件事都**必须**反映到结果里。
"""

import argparse
import os
import re
import sys

AGENT_DIRS = {"beacon_agent": "beacon", "linux_agent": "linux"}

# Go 类型 -> 控件。*T 一律先去掉指针再看。
TYPE_WIDGET = {
    "string": "text",
    "int": "number",
    "int32": "number",
    "int64": "number",
    "float64": "number",
    "bool": "bool",
}

# ax 里 showif 的简写到下发契约的映射。下发用的键必须带 _in 后缀（契约见
# doc §6.18 与前端的 BuildSchemaField），ax 里允许写短的。
SHOWIF_KEYS = {"format": "format_in", "format_in": "format_in",
               "listener": "listener_in", "listener_in": "listener_in"}

HINT_START = "/* "
GEN_NOTE = (
    "   本函数由 tools/gen_build_schema.py 从插件 Go 源码生成，**不要手改** ——\n"
    "   改了下次生成会被覆盖。要改标签/分组/提示，去改 pl_main.go 里对应字段的\n"
    '   ax:"..." tag；要改取值域，去改插件里那个字段实际比较的字符串常量。\n'
)


# ── Go 侧解析 ──────────────────────────────────────────────────────────────

def go_sources(agent_dir):
    """这个 agent 目录下所有 .go 源码（选项列表可能散在 pl_utils.go 等文件里）。"""
    out = {}
    for name in sorted(os.listdir(agent_dir)):
        if name.endswith(".go"):
            out[name] = open(os.path.join(agent_dir, name), "rb").read().decode("utf-8")
    return out


def parse_struct(src):
    """取 GenerateConfig 的字段表：[(GoName, GoType, jsonTag, axTag)]。"""
    m = re.search(r"type\s+GenerateConfig\s+struct\s*\{", src)
    if not m:
        raise ValueError("找不到 type GenerateConfig struct")
    i, depth, body = m.end(), 1, []
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
    struct_body = "".join(body)

    fields = []
    for line in struct_body.split("\n"):
        fm = re.match(r"\s*(\w+)\s+([^\s`]+)\s+`([^`]*)`\s*$", line)
        if not fm:
            continue
        goname, gotype, tagstr = fm.group(1), fm.group(2), fm.group(3)
        jm = re.search(r'\bjson:"([^"]+)"', tagstr)
        if not jm:
            continue
        axm = re.search(r'\bax:"([^"]*)"', tagstr)
        fields.append((goname, gotype, jm.group(1).split(",")[0],
                       axm.group(1) if axm else ""))
    return fields


def parse_ax_tag(tag):
    """ax:"label=代理端口;group=代理与轮换;range=1-65535;extra=a,b;showif=..." """
    out = {}
    for part in tag.split(";"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, v = part.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def _switch_cases(src, goname):
    out = []
    for m in re.finditer(r"switch\s+generateConfig\.%s\s*\{" % re.escape(goname), src):
        i, depth = m.end(), 1
        while i < len(src) and depth > 0:
            c = src[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block, depth = src[m.end():i], 0
        for line in block.split("\n"):
            if depth == 0:
                cm = re.match(r'\s*case\s+"([^"]*)"', line)
                if cm:
                    out.append(cm.group(1))
            depth += line.count("{") - line.count("}")
    return out


def go_literals(sources, goname):
    """这个字段在所有 Go 源码里被实际比较过的字符串常量（去重保序）。

    ★ 必须查**整个包**，不能只查 pl_main.go：beacon 的 dns_mode 那个 switch
      在 pl_utils.go:495，只查主文件会一条都抽不到 —— 而「抽不到」和
      「这个字段真没有取值域」在结果上完全一样，是静默失效。
    """
    seen, out = set(), []
    for src in sources.values():
        lits = re.findall(r'generateConfig\.%s\s*(?:==|!=)\s*"([^"]*)"' % re.escape(goname), src)
        lits += _switch_cases(src, goname)
        for lit in lits:
            if lit not in seen:
                seen.add(lit)
                out.append(lit)
    return out


def pretty(key):
    """回退用的标签：proxy_port -> Proxy Port。"""
    return " ".join(w.capitalize() for w in key.split("_") if w)


# ── JS 生成 ────────────────────────────────────────────────────────────────

def _js_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _js_list(items):
    return "[" + ", ".join(_js_str(i) for i in items) + "]"


def build_fields(agent, sources, struct_src):
    fields, notes = [], []
    for goname, gotype, key, axtag in parse_struct(struct_src):
        ax = parse_ax_tag(axtag)
        base = gotype.lstrip("*")
        widget = ax.get("type") or TYPE_WIDGET.get(base, "text")

        lits = go_literals(sources, goname)
        extra = [e.strip() for e in ax.get("extra", "").split(",") if e.strip()]
        for e in extra:
            if e in lits:
                notes.append("%s: ax 的 extra=%r 已经能从 Go 里抽到，可以删掉了" % (key, e))
        options = lits + [e for e in extra if e not in lits]
        if options:
            widget = "select"

        # optorder：把点名的取值提到前面，剩下的按推导顺序追加。
        # ★ 只能重排，不能增删 —— 否则它就变成了第二份「有哪些选项」的声明，
        #   漏写一个值就把那个值藏了，而「藏了」和「插件本来没这个值」在界面上
        #   一模一样。所以点到推导结果之外的值是硬错误，不是静默忽略。
        if ax.get("optorder"):
            want = [v.strip() for v in ax["optorder"].split(",") if v.strip()]
            missing = [v for v in want if v not in options]
            if missing:
                raise ValueError(
                    "%s: ax 的 optorder 点了 %s，但推导出来的选项是 %s "
                    "—— optorder 只能重排已有选项（要多一个值请用 extra=）"
                    % (key, missing, options))
            options = want + [o for o in options if o not in want]

        if base not in TYPE_WIDGET:
            notes.append("%s: Go 类型 %s 没有映射，按输入框处理" % (key, gotype))

        f = {"key": key, "label": ax.get("label") or pretty(key),
             "widget": widget, "group": ax.get("group", "未分组"),
             "_tagged": bool(axtag)}
        # order = 组内位置，gorder = 组的位置。都只影响排序，不写就是 0
        # （照 flexbox 的 order 语义：默认 0，小的在前，同值按结构体顺序）。
        for k, dst in (("order", "_order"), ("gorder", "_gorder")):
            if k in ax:
                try:
                    f[dst] = int(ax[k])
                except ValueError:
                    raise ValueError("%s: ax 的 %s=%r 不是整数" % (key, k, ax[k]))
        if options:
            f["options"] = options
        if "range" in ax:
            lo, _, hi = ax["range"].partition("-")
            f["min"], f["max"] = lo.strip(), hi.strip()
        if ax.get("hint"):
            f["hint"] = ax["hint"]
        if ax.get("showif"):
            cond = {}
            for clause in ax["showif"].split("+"):
                k, _, v = clause.partition(":")
                k = SHOWIF_KEYS.get(k.strip())
                if k is None:
                    # ★ 必须是硬错误。写错键名（frmat:）会生成一条前端不认识的
                    #   条件，字段于是**永远不显示** —— 而"永远不显示"和"这字段
                    #   本来就不用显示"在界面上长得一模一样，属于静默失效。
                    raise ValueError(
                        "%s: ax 里 showif 的条件键 %r 不认识，只认 %s"
                        % (key, clause.partition(":")[0].strip(),
                           "/".join(sorted(SHOWIF_KEYS))))
                cond[k] = [x.strip() for x in v.split(",") if x.strip()]
            f["show_if"] = cond
        if ax.get("file"):
            f["widget"], f["accept"] = "file", ax["file"]
        fields.append((key, f))
    return fields, notes


def order_by_group(fields):
    """排成最终的先后：组间看 gorder，组内看 order，都不写就是 0。

    组的先后 = gorder（同值按该组第一个成员在结构体里的位置）；
    组内先后 = order（同值按结构体顺序）。

    ★ 两级必须**各自独立**算，组的次序不能从「排完序之后的字段次序」去推。
      否则一个组的两个字段只要被 order= 拉开到两端，这个组就会在表里裂成
      两段 —— 而前端是按**连续段**折叠的，裂开就会长出两个同名折叠块。
      独立算的话，同组必然连续，这个坑从结构上就不存在。
    """
    gpos = {}
    for _, f in fields:
        if f["group"] not in gpos:
            gpos[f["group"]] = len(gpos)          # 该组第一个成员的结构体位置
    gorder = {}
    for _, f in fields:
        g = f["group"]
        if "_gorder" in f:
            if g in gorder and gorder[g] != f["_gorder"]:
                raise ValueError(
                    "组 %r 上有两个不同的 gorder（%r 与 %r）—— 同一个组只能有一个位置"
                    % (g, gorder[g], f["_gorder"]))
            gorder[g] = f["_gorder"]
    groups = sorted(gpos, key=lambda g: (gorder.get(g, 0), gpos[g]))

    # sorted 是稳定排序：先按 order 排一次（同值保持结构体顺序），
    # 再按组排一次（同组保持上一步的结果）。
    fields = sorted(fields, key=lambda kf: kf[1].get("_order", 0))
    return sorted(fields, key=lambda kf: groups.index(kf[1]["group"]))


def render(agent, fields, source_note):
    """渲染成 .axs 里的 JS。重复出现的取值域抽成命名常量。"""
    # 同一条 show_if 列表用 >=2 次就提成常量
    tally = {}
    for _, f in fields:
        for k, v in (f.get("show_if") or {}).items():
            tally[(k, tuple(v))] = tally.get((k, tuple(v)), 0) + 1
    consts, names = [], {}
    for (k, v), n in tally.items():
        if n < 2:
            continue
        name = ("L_" if k == "listener_in" else "F_") + re.sub(r"\W+", "_", v[0]).upper()[:12]
        while name in names.values():
            name += "_"
        names[(k, v)] = name
        consts.append((name, list(v)))

    out = []
    out.append("/* " + "=" * 73)
    out.append("   BuildSchema -- 给 Web 控制台（AdaptixWeb）的生成参数表。")
    out.append("")
    out.append("   与上面 GenerateUI 并列、互不影响：GenerateUI 给 Qt 客户端本地渲染，")
    out.append("   BuildSchema 只返回纯数据给 Web 前端。GenerateUI 不归这个脚本管。")
    out.append("")
    out.append("   *** 以下是生成物，不要手改 ***")
    out.append("")
    out.append(GEN_NOTE.rstrip("\n"))
    out.append("")
    out.append("   真相来源（%s）：" % source_note)
    out.append("     字段(key)/类型   <- GenerateConfig 的 json tag + Go 类型   ← 增删字段只改这里")
    out.append("     选项             <- 代码里实际比较过的字符串常量（== / != / switch case）")
    out.append("     标签/分组/提示/显隐 <- 字段上的 ax:\"...\" tag，没写的按 key 美化作回退")
    out.append("     排列顺序         <- tag 里的 order= / gorder= / optorder=，见下")
    out.append("")
    out.append("   ★ order 系列只能【重排】，永远不能增删。三个键默认都是 0，都是「小的排前面」，")
    out.append("     撞值就按字段在结构体里的先后：")
    out.append("       order=N        这个字段在本组里的位置")
    out.append("       gorder=N       整个组的位置（组序是独立算的，所以一个组不会被切成两段）")
    out.append("       optorder=a,b   把点名的选项提到最前，没点名的按推导顺序接在后面")
    out.append("     举例：format 的 optorder 把 Stager 放到末位，只是「显示在后面」；插件以后")
    out.append("     新增一个 Format == \"Stager\" 之外的分支，新值会【自动出现在列表里】（排最后），")
    out.append("     不会像写死的清单那样把它悄悄藏掉 —— 这正是这几个键只做重排的原因。")
    out.append("     代价：optorder 点到 Go 里不存在的值会【硬报错】，逼你回去改那一行。")
    out.append("")
    out.append("   要加字段 / 改选项 / 改标签，一律去改插件的 .go 源码，别在这个文件里改：")
    out.append("       python tools/gen_build_schema.py --agent beacon           # 预览")
    out.append("       python tools/gen_build_schema.py --agent beacon --write    # 落盘")
    out.append("   改完再跑 tools/check_build_schema.py 对账（带阳性对照）。")
    out.append("   " + "=" * 73 + " */")
    out.append("function BuildSchema(listener_type)")
    out.append("{")
    if consts:
        out.append("    // 重复出现的取值域")
        for name, vals in consts:
            out.append("    let %s = %s;" % (name, _js_list(vals)))
        out.append("")
    out.append("    return {")
    out.append("        version: 1,")
    out.append("        fields: [")
    cur = None
    for _, f in order_by_group(fields):
        if f["group"] != cur:
            cur = f["group"]
            out.append("            // -- %s %s" % (cur, "-" * max(0, 56 - len(cur))))
        parts = ['key: %s' % _js_str(f["key"]),
                 'label: %s' % _js_str(f["label"]),
                 'type: %s' % _js_str(f["widget"])]
        if f.get("options"):
            parts.append("options: %s" % _js_list(f["options"]))
        if "min" in f:
            parts.append("min: %s, max: %s" % (f["min"], f["max"]))
        if f.get("accept"):
            parts.append("accept: %s" % _js_str(f["accept"]))
        parts.append("group: %s" % _js_str(f["group"]))
        if f.get("show_if"):
            cond = ", ".join("%s: %s" % (k, names.get((k, tuple(v)), _js_list(v)))
                             for k, v in f["show_if"].items())
            parts.append("show_if: { %s }" % cond)
        if f.get("hint"):
            parts.append("hint: %s" % _js_str(f["hint"]))
        out.append("            { %s }," % ", ".join(parts))
    out.append("        ]")
    out.append("    };")
    out.append("}")
    return "\n".join(out) + "\n"


def split_at_schema(src, axs_path):
    """把 .axs 切成 (BuildSchema 之前的所有内容, 之后的所有内容)。

    切点是 BuildSchema 上面那段注释块的开头 —— 生成物连同它的头注释一起换掉，
    头注释也是生成物的一部分。

    ★ 切点只有这一处定义。落盘后的"前缀没被动过"校验必须调它，不能另写一套
      等价逻辑 —— 两套实现只要有一处不一致，"改了但没生效"就会从缝里漏过去。
    """
    lines = src.split("\n")
    hits = [n for n, l in enumerate(lines) if l.startswith("function BuildSchema(listener_type)")]
    # ★ 两处 BuildSchema 说明这文件已经被谁手改过；只换第一处会把另一处留在下面，
    #   而 JS 里后声明的函数会覆盖先声明的 —— 结果是"改了但没生效"，最难查的那种。
    if len(hits) != 1:
        raise ValueError("%s 里有 %d 个 function BuildSchema，只该有 1 个" % (axs_path, len(hits)))
    m = hits[0]
    while m > 0 and not lines[m].startswith("/*"):
        m -= 1
    if m == 0:
        raise ValueError("%s: BuildSchema 前面没找到注释锚点，不敢动这个文件" % axs_path)
    return "\n".join(lines[:m]) + "\n", "\n".join(lines[m:])


def splice(axs_path, js):
    """把旧 BuildSchema（含它前面那段注释）整段换掉，其余字节不动。"""
    src = open(axs_path, "rb").read().decode("utf-8")
    prefix, _ = split_at_schema(src, axs_path)
    return prefix + js


def write_axs(axs_path, js):
    """落盘。这是唯一会改文件的路径，所以每一步都留痕、每个前提都先验。

    四道闸：① 先把原文件备份到临时目录并打印路径（**不放进交付树** ——
    树里的文件数是被 verify_dev.py 数着的）；② splice 只换 BuildSchema 那一段，
    前缀必须逐字节不变；③ GenerateUI 的出现次数改前改后必须一样（Qt 那条路
    一个字都不许动）；④ 先写临时文件再 os.replace，避免写一半把 .axs 截断。
    """
    import hashlib
    import tempfile

    old = open(axs_path, "rb").read().decode("utf-8")
    if "\r\n" in old:
        raise ValueError("%s 里有 CRLF，先统一成 LF 再说" % axs_path)

    old_md5 = hashlib.md5(old.encode("utf-8")).hexdigest()
    new = splice(axs_path, js)
    new_md5 = hashlib.md5(new.encode("utf-8")).hexdigest()

    if old.count("function GenerateUI") != new.count("function GenerateUI"):
        raise AssertionError("GenerateUI 的个数被改动了 —— 这是 Qt 那条路，不许碰")
    # splice 的语义就是"只换尾巴"，前缀必须一个字节都没动
    prefix, _ = split_at_schema(old, axs_path)
    if not new.startswith(prefix):
        raise AssertionError("BuildSchema 之前的内容被动过了")
    if not new[len(prefix):].startswith("/*"):
        raise AssertionError("新内容的开头不是生成物的头注释，切点可疑")

    bak = os.path.join(tempfile.gettempdir(), "axs-bak",
                       os.path.basename(os.path.dirname(axs_path)) + "." + old_md5[:8] + ".axs")
    os.makedirs(os.path.dirname(bak), exist_ok=True)
    if not os.path.exists(bak):
        open(bak, "wb").write(old.encode("utf-8"))

    tmp = axs_path + ".tmp-write"
    open(tmp, "wb").write(new.encode("utf-8"))
    os.replace(tmp, axs_path)

    print("         -> 已写入 %s" % os.path.basename(axs_path))
    print("            md5 %s -> %s   %d -> %d 字节"
          % (old_md5, new_md5, len(old.encode("utf-8")), len(new.encode("utf-8"))))
    print("            原文件备份：%s" % bak)


# ── 主流程 ─────────────────────────────────────────────────────────────────

def run(extenders, agent_filter=None):
    results = {}
    for d in sorted(os.listdir(extenders)):
        agent = AGENT_DIRS.get(d, d)
        if agent_filter and agent != agent_filter:
            continue
        adir = os.path.join(extenders, d)
        axs = os.path.join(adir, "ax_config.axs")
        if not (os.path.isdir(adir) and os.path.isfile(axs)):
            continue
        sources = go_sources(adir)
        main = sources.get("pl_main.go")
        if not main or "type GenerateConfig struct" not in main:
            continue
        fields, notes = build_fields(agent, sources, main)
        note = "GenerateConfig in pl_main.go；取值域扫了 %d 个 .go" % len(sources)
        results[agent] = {"axs": axs, "fields": fields, "notes": notes,
                          "js": render(agent, fields, note)}
    return results


def selftest(extenders):
    """阳性对照：往真源码里塞一个假字段 / 改一个真常量，两者都必须反映到生成结果里。"""
    targets = []
    for d in sorted(os.listdir(extenders)):
        p = os.path.join(extenders, d, "pl_main.go")
        if not os.path.isfile(p):
            continue
        if "type GenerateConfig struct" in open(p, "rb").read().decode("utf-8", "replace"):
            targets.append(d)
    if not targets:
        print("SELFTEST FAIL: 找不到任何有 GenerateConfig 的插件")
        return 1
    d = targets[0]
    agent = AGENT_DIRS.get(d, d)
    adir = os.path.join(extenders, d)
    sources = go_sources(adir)
    main = sources["pl_main.go"]

    base_fields, _ = build_fields(agent, sources, main)
    if len(base_fields) < 5:
        print("SELFTEST FAIL: 只解析出 %d 个字段，链条本身就没跑起来" % len(base_fields))
        return 1
    if not any(f.get("options") for _, f in base_fields):
        print("SELFTEST FAIL: 没有任何字段抽到选项 —— 选项那条判据是空转的")
        return 1
    n_opt = sum(1 for _, f in base_fields if f.get("options"))
    print("SELFTEST info : %s 解析出 %d 个字段，其中 %d 个带自动抽取的选项"
          % (agent, len(base_fields), n_opt))

    bad = 0

    # (a) 往结构体里塞一个假字段 -> 必须出现在生成结果里
    mut = main.replace("type GenerateConfig struct {",
                       'type GenerateConfig struct {\n\tZzProbe string `json:"zz_probe"`', 1)
    f2, _ = build_fields(agent, dict(sources, **{"pl_main.go": mut}), mut)
    hit = [f for k, f in f2 if k == "zz_probe"]
    if hit:
        print("SELFTEST ok   : 新增字段 zz_probe -> 自动出现在表里，标签回退为 %r" % hit[0]["label"])
    else:
        print("SELFTEST FAIL : 往 Go 里加的字段没有出现在生成结果里")
        bad += 1

    # (b) 把某个字段在 Go 里被比较的常量改名 -> 必须反映到那个字段的选项里
    #     ★ 必须改**全部**出现处：`generateConfig.Arch == "x86"` 在 pl_main.go 里有
    #       10 处，只改一处的这个常量仍然能从别处抽到，断言会假失败（第一版就栽在这）。
    #     ★ 跳过带 optorder 的字段：改名之后 optorder 还指着旧名，那是**该报错**的
    #       （见 (g)），走不到这里。混在一起会让这条对照变成假失败。
    axtags = {jk: at for _, _, jk, at in parse_struct(main)}
    done = False
    for key, f in base_fields:
        if not f.get("options") or "optorder" in axtags.get(key, ""):
            continue
        goname = next(g for g, _, jk, _ in parse_struct(main) if jk == key)
        old = f["options"][0]
        mut_main = re.sub(
            r'(generateConfig\.%s\s*(?:==|!=)\s*)"%s"' % (re.escape(goname), re.escape(old)),
            r'\g<1>"__mutated__"', main)
        if mut_main == main:
            continue
        f3, _ = build_fields(agent, dict(sources, **{"pl_main.go": mut_main}), mut_main)
        opts3 = dict(f3)[key]["options"]
        if old in opts3:
            continue          # 这个常量在别处也被比较，换一个字段再试
        if "__mutated__" in opts3:
            print("SELFTEST ok   : Go 里把 %s 的 %r 改名 -> 选项跟着变 (%s)"
                  % (goname, old, ", ".join(opts3)))
            done = True
            break
    if not done:
        print("SELFTEST FAIL : 找不到能验证「改 Go 常量 -> 选项跟着变」的字段")
        bad += 1

    # (c) 生成的 key 集合必须与结构体 tag 集合完全相等
    tags = [jk for _, _, jk, _ in parse_struct(main)]
    keys = [k for k, _ in base_fields]
    if tags == keys:
        print("SELFTEST ok   : 生成的 %d 个 key 与 GenerateConfig 的 %d 个 json tag 逐一对应"
              % (len(keys), len(tags)))
    else:
        print("SELFTEST FAIL : key 集合对不上 %s vs %s" % (keys, tags))
        bad += 1

    # (d) order / gorder / optorder **只能重排，不能增删**。
    #     这是它跟「第二份手抄本」的分界线，所以必须真的测，不能只写在注释里：
    #     加上顺序标签之后，key 集合与每个字段的选项集合都必须**一个字不变**。
    withord = re.sub(r'(ax:"[^"]*?)"', r'\1;order=-3;gorder=-5"', main)
    n = len(re.findall(r';gorder=-5"', withord))
    if n < 2:
        print("SELFTEST FAIL : 没给足够多的字段塞上顺序标签，这条对照是空转的")
        bad += 1
    else:
        f4, _ = build_fields(agent, dict(sources, **{"pl_main.go": withord}), withord)
        same_keys = [k for k, _ in f4] == keys
        canon = lambda fs: {k: f.get("options") for k, f in fs}
        same_opts = canon(f4) == canon(base_fields)
        ordered = order_by_group(f4)
        moved = [k for k, _ in ordered] != keys
        if same_keys and same_opts and moved:
            print("SELFTEST ok   : 给 %d 个字段加 order/gorder -> 顺序变了、"
                  "字段集合与选项集合一个没变" % n)
        else:
            print("SELFTEST FAIL : 顺序标签动了内容（keys 同=%s opts 同=%s 顺序变了=%s）"
                  % (same_keys, same_opts, moved))
            bad += 1

        # 组内必须连续 —— 前端是按连续段折叠的，裂开就会长出同名折叠块
        gs = [f["group"] for _, f in ordered]
        runs = [g for i, g in enumerate(gs) if i == 0 or g != gs[i - 1]]
        if len(set(runs)) == len(runs):
            print("SELFTEST ok   : 乱加 order 之后同组仍然连续（无重复组名块）")
        else:
            print("SELFTEST FAIL : 顺序标签把某个组拆成了两段 %s" % runs)
            bad += 1

        # 组内次序 = (order 升序, 同值按结构体位置)。用**手写期望**的合成输入来验，
        # 而不是拿真实数据再算一遍 —— 那等于拿实现验证实现，永远通过。
        fake = [("a", {"group": "G", "_order": 2}),
                ("b", {"group": "G"}),
                ("c", {"group": "G", "_order": -1}),
                ("d", {"group": "G", "_order": -1})]
        got = [k for k, _ in order_by_group(fake)]
        if got == ["c", "d", "b", "a"]:
            print("SELFTEST ok   : 组内次序 = (order 升序, 同值按结构体位置) -> %s" % got)
        else:
            print("SELFTEST FAIL : 组内次序不符，期望 ['c','d','b','a'] 实得 %s" % got)
            bad += 1

        # 组的次序按 gorder；且**一个组即使被 order 拉得很开也不能裂成两段**
        # （前端按连续段折叠，裂了就长出同名折叠块）。
        fake2 = [("p1", {"group": "P", "_order": 9}),
                 ("q1", {"group": "Q", "gorder_note": 1, "_gorder": 1}),
                 ("p2", {"group": "P", "_order": -9}),
                 ("q2", {"group": "Q", "_gorder": 1})]
        got2 = [k for k, _ in order_by_group(fake2)]
        if got2 == ["p2", "p1", "q1", "q2"]:
            print("SELFTEST ok   : 组按 gorder 排且不裂段 -> %s" % got2)
        else:
            print("SELFTEST FAIL : 期望 ['p2','p1','q1','q2'] 实得 %s" % got2)
            bad += 1

    # (f) 生成必须是确定的：同一份源码跑两次，结果逐字节相同。
    #     否则「改了标签没生效」和「生成器本身不稳定」会混在一起分不清。
    j1 = render(agent, build_fields(agent, sources, main)[0], "x")
    j2 = render(agent, build_fields(agent, sources, main)[0], "x")
    if j1 == j2:
        print("SELFTEST ok   : 同一份源码生成两次，结果逐字节相同")
    else:
        print("SELFTEST FAIL : 生成不确定（两次结果不同）")
        bad += 1

    # (e) 顺序/选项标签写错必须硬报错。三种写错的方式都要炸：
    #     optorder 点到不存在的值、order 不是整数、同组两个 gorder 打架。
    #     ★ 用 json tag 当锚点注入，不要拿 `group=基础"` 这种串去 replace ——
    #       group 不是标签的最后一项时那个 `"` 根本不在，replace 会一声不响地
    #       什么都不做，于是阳性对照变成"造不出错"，看起来像通过了。
    def _add_to_ax(src, key, extra):
        # json:"x" 与 ax:"..." 在**同一对反引号里**，中间没有反引号。
        pat = r'(json:"%s"\s+ax:")([^"]*)(")' % re.escape(key)
        new, n = re.subn(pat, lambda m: m.group(1) + m.group(2) + extra + m.group(3),
                         src, count=1)
        if not n:
            raise AssertionError("注入锚点没命中：json:%r 上没有紧跟 ax 标签" % key)
        return new

    def _ordered(src):
        # 走和正式生成一样的路径：build_fields 之后还要 order_by_group
        #（gorder 打架是在 order_by_group 里查的）
        fs, _ = build_fields(agent, dict(sources, **{"pl_main.go": src}), src)
        order_by_group(fs)
        return fs

    k0 = keys[0]
    grp_of = dict(base_fields)
    # 找一个至少有 2 个成员的组，用来制造 gorder 打架
    pair = None
    for a in base_fields:
        for b in base_fields:
            if a[0] != b[0] and a[1]["group"] == b[1]["group"]:
                pair = (a[0], b[0])
                break
        if pair:
            break

    cases = [("optorder 点到推导结果之外的值", _add_to_ax(main, k0, ";optorder=__nope__")),
             ("order 不是整数", _add_to_ax(main, k0, ";order=abc"))]
    if pair:
        m2 = _add_to_ax(main, pair[0], ";gorder=1")
        cases.append(("同一个组上有两个不同的 gorder",
                      _add_to_ax(m2, pair[1], ";gorder=2")))
    else:
        print("SELFTEST FAIL : 找不到同组两个字段，gorder 打架这条对照无效")
        bad += 1

    for name, mut in cases:
        try:
            _ordered(mut)
            print("SELFTEST FAIL : %s 竟然没报错" % name)
            bad += 1
        except ValueError as e:
            print("SELFTEST ok   : %s -> 报错(%s)" % (name, str(e)[:56]))

    # (g) 在 Go 里把某个取值改了名，而 ax 的 optorder 还指着旧名 -> 必须报错。
    #     这是真实会发生的事（改常量的人不会记得去翻 struct tag），也是把 optorder
    #     做成"只重排"的直接代价。报错就是在提醒他去改那一行 —— 总好过静默地
    #     保持一个指向空气的顺序声明。
    opt_keys = [k for k, _ in base_fields if "optorder" in axtags.get(k, "")]
    if not opt_keys:
        print("SELFTEST FAIL : 没有一个字段带 optorder，这条对照无效")
        bad += 1
    else:
        # ★ 必须改一个**真的在 Go 里被比较过**的常量。arch 的首选项是 x64，
        #   而 x64 来自 ax 的 extra=（Go 里没有 `Arch == "x64"`），拿它去改名
        #   等于什么都没改，这条对照会变成假通过。
        fired = False
        for opt_key in opt_keys:
            goname = next(g for g, _, jk, _ in parse_struct(main) if jk == opt_key)
            for lit in go_literals(sources, goname):
                ren = main.replace('"%s"' % lit, '"__renamed__"')
                if ren == main:
                    continue
                try:
                    _ordered(ren)
                    continue          # 没炸，换一个常量再试
                except ValueError as e:
                    print("SELFTEST ok   : Go 里把 %s 的 %r 改名而 optorder 没跟着改"
                          " -> 报错(%s)" % (goname, lit, str(e)[:48]))
                    fired = True
                    break
            if fired:
                break
        if not fired:
            print("SELFTEST FAIL : optorder 指着已被改名的取值，竟然没报错")
            bad += 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    default_ext = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "AdaptixServer", "extenders")
    ap.add_argument("--extenders", default=default_ext)
    ap.add_argument("--agent", help="只处理一个 agent（beacon / linux）")
    ap.add_argument("--write", action="store_true", help="写进 ax_config.axs（默认只预览）")
    ap.add_argument("--print", dest="show", action="store_true",
                    help="把要写的 JS 原文打到 stdout")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.extenders)
    if not os.path.isdir(args.extenders):
        print("extenders 目录不存在：%s" % args.extenders)
        return 2

    res = run(args.extenders, args.agent)
    if not res:
        print("没有找到可生成的 agent")
        return 1

    total = 0
    for agent, r in res.items():
        auto = sum(1 for _, f in r["fields"] if f.get("options"))
        # ★ 判据是「字段上有没有 ax: tag」，不是「label 跟美化过的 key 像不像」。
        #   后者会把 sleep（label 就该是 "Sleep"）误报成没打标签。
        tagged = sum(1 for _, f in r["fields"] if f.get("_tagged"))
        untagged = [k for k, f in r["fields"] if not f.get("_tagged")]
        print("%-8s %2d 个字段（%2d 个自动抽到选项，%2d 个有 ax 标签）"
              % (agent, len(r["fields"]), auto, tagged))
        for n in r["notes"]:
            print("         note: %s" % n)
        if untagged:
            print("         没打 ax 标签（标签回退成英文 key，控件仍是推导出来的）：%s"
                  % " ".join(untagged))
        if args.show:
            print(r["js"])
        if args.write:
            write_axs(r["axs"], r["js"])
        total += len(r["fields"])
    print("---- 共 %d 个字段%s" % (total, "" if args.write else "（预览模式，未写盘）"))
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())
