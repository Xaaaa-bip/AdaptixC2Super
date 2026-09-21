#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p23 — 静态判据：BeaconDataExtract 的返回值**不是 C 字符串**。

## 不变量（ISS-073）

`bof_api.c` 的 `BeaconDataExtract(parser, &len)` 返回**长度切片的裸指针**：它把
`parser->buffer` 原样返回、只把长度写进 `*len`，**不写 NUL、也不保证切片后有 NUL**。
而 agent 收到的 args 是 msgpack 报文 `{content, args, entry_func}`（`elf_bof.c:753`）
里 `args` 字段的一个切片 —— **切片后面紧跟同一缓冲区里的下一个 msgpack 字段**。

于是"把切片当 C 字符串用"（`%s` / 按路径取文件的 API / `AxStrlen`）会一路读进相邻内存；
是否出事**取决于切片后那个字节恰好是不是 0** ⇒ 平时表现为"偶尔能跑通"，极难靠功能测试发现。

实测（p22 首跑，ko_load.c）：
    [-] ko_load: cannot open /tmp/p22ko/axp22probe.ko<垃圾>entry_func<垃圾>go
尾巴上的 `entry_func` / `go` 正是 msgpack 的同级键值。

**这个陷阱在本仓库已经咬过一次**（`ebpf_load.c:481-483` 的注释）：
按 NUL 扫描把下一个字段的长度字节折进了 map key（`"name" -> "name\\x06"`），
于是 key 永远匹配不上真实文件名，**hide 静默失效**。

## 判据形态：问"消费方拿到长度了吗"

对每个 `X = BeaconDataExtract(&P, &L)`，找包含 X 的**最内层调用** `NAME(...)`：

  * 调用文本里**出现长度变量 L** → **SAFE**。消费方被明确告知了长度，本来就不需要 NUL。
    这一条同时覆盖了 `ax_cstr(dst, sz, X, L)`、`AxMemcpy(dst, X, L)`、
    `AxWriteFile(fd, X, L)`、`AxStrncmp(X, .., L)`、`AxBpf(.., X, L)` —— 不必逐个枚举 API 名。
  * 调用文本里有 `%s` → **BARE**，且按消费方标出严重度：
      - `%s(落盘)`：`AxSnprintf`/`AxStrcpy`/`AxCat` 等 —— 垃圾会**写进产物**（IOC）。
      - `%s(回执)`：`BeaconPrintf` —— 垃圾会**进 C2 日志**，属内存泄露（把相邻 msgpack 字段发回服务端）。
  * 其余需要 NUL 的 API（`AxOpenFile`/`AxReadFileToBuffer`/`AxStrlen`…）→ **BARE**。

**为什么规则是"长度在不在调用里"而不是"扫 API 黑名单"**：黑名单永远列不全，且会把
已经正确处理（`ax_cstr`）的站点误报 —— 本判据的第一版就是这么误报了 5 处的。
判据一旦开始误报，就会被当成噪音然后被绕过。

## 正确写法

    char buf[256];
    if (len >= (int)sizeof(buf)) { 报错; return; }
    AxMemcpy(buf, X, len);
    buf[len] = '\\0';          /* 或复用现成的 ax_cstr(buf, sizeof buf, X, len) */

参考实现：`cron_persist.c`(jobname)、`memfd_exec.c`(fpath)、`ebpf_load.c`(pathbuf/targetbuf)。
"""

import os
import re
import sys

BOFS = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bofs"

EXTRACT = re.compile(
    r"char\s*\*\s*(\w+)\s*=\s*BeaconDataExtract\s*\(\s*&\w+\s*,\s*&(\w+)\s*\)\s*;")

# 需要 NUL 的 API（调用文本里既没有长度、也没有 %s，但仍然是字符串消费方）
CSTR_APIS = ("AxOpenFile", "AxReadFileToBuffer", "AxStrlen", "AxStrstr", "AxStrchr",
             "AxRealPath", "AxFopen", "AxStat")
# 会把参数**写进产物**的格式化/拼接调用
DISK_SINKS = ("AxSnprintf", "AxStrcpy", "AxStrcat", "AxStrncpy")


def enclosing_call(body, pos):
    """返回包含 body[pos] 的最内层调用 (name, text)。找不到返回 (None, '')。

    ★ 必须**在语句边界收手**：变量不总是在调用里（也可能出现在
    `for (i = 0; i < pin_len; i++)`、赋值、`if` 条件里）。若不加这个边界，
    向后找 `(` 会一路越过语句、撞上**前面某个不相干的调用**，于是把那个调用的
    `%s` 算到这个变量头上 —— 第一版就是这么误报了 5 处的（hide_del 的 pinbuf、
    ssh_persist 的字面量 path、ebpf_load 的 dst 都被算到了裸用变量头上）。
    """
    depth = 0
    i = pos
    while i > 0:
        c = body[i]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                j = i - 1
                while j >= 0 and (body[j].isalnum() or body[j] == "_"):
                    j -= 1
                name = body[j + 1:i]
                d, k = 0, i
                while k < len(body):
                    if body[k] == "(":
                        d += 1
                    elif body[k] == ")":
                        d -= 1
                        if d == 0:
                            break
                    k += 1
                return name, body[i:k + 1]
            depth -= 1
        elif depth == 0 and c in ";{}":
            return None, ""          # 语句边界：这个变量不在任何调用里
        i -= 1
    return None, ""


def mask(src):
    """把注释与字符串/字符字面量抹成空格，**保持偏移与行号不变**。

    ★ 为什么必须抹（第二版判据的 5 处误报全出在这里）：
      `\\bpin\\b` 会命中字符串里的 "<pin>_map0"、`\\bpath\\b` 会命中
      "hand to helper via pinned path"、`\\bkey\\b` 会命中中文注释里的 "key"。
      于是判据把**某个真实存在的调用**的 `%s` 算到了这些假"使用点"头上，
      而这些调用其实用的是已经拷好的有终止缓冲（pinbuf / g_pin / namebuf / dst）。
      判据的第一要务是**不误报** —— 一旦开始误报就会被当成噪音绕过。
    注意：`%s` 恰好在字符串里，所以检测 `%s` 时必须用**原文**的调用文本；
    这里只用抹过的文本来定位**标识符的使用点**，两者偏移一致，可混用。
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif c in "\"'":
            q = c
            out[i] = " "
            i += 1
            while i < n and src[i] != q:
                if src[i] == "\\" and i + 1 < n:
                    out[i] = out[i + 1] = " "
                    i += 2
                    continue
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def analyze(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    masked = mask(src)
    rows = []
    for m in EXTRACT.finditer(masked):
        var, ln = m.group(1), m.group(2)
        end = masked.find("\n}", m.end())
        end = end if end > 0 else len(masked)
        body_m = masked[m.end():end]        # 定位使用点用抹过的
        body_o = src[m.end():end]           # 取调用文本（含 %s）用原文
        verdict, why, bad = "SAFE", "消费方都拿到了长度", []
        for u in re.finditer(r"\b" + re.escape(var) + r"\b", body_m):
            name, call = enclosing_call(body_o, u.start())
            if name is None:
                continue
            if re.search(r"\b" + re.escape(ln) + r"\b", call):
                continue                      # ① 长度在同一调用里 → 安全
            if "%s" in call or "%p" in call or "%d" in call:
                sev = "%s(落盘)" if name in DISK_SINKS else "%s(回执)"
                bad.append(("%s @%s" % (sev, name), u.start()))
            elif name in CSTR_APIS:
                bad.append(("%s(需NUL)" % name, u.start()))
        if bad:
            verdict = "BARE"
            ln_no = src.count("\n", 0, m.end() + bad[0][1]) + 1
            lines = src.splitlines()
            snippet = lines[ln_no - 1].strip() if 0 < ln_no <= len(lines) else ""
            why = "第%d行 裸用：%s\n         │ %s" % (
                ln_no, "、".join(sorted({b[0] for b in bad})), snippet[:120])
        rows.append((var, ln, verdict, why))
    return rows


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    n_safe = n_bare = 0
    bare = []
    print("=== p23 静态判据：BeaconDataExtract 切片不得裸当 C 字符串 ===")
    for fn in sorted(os.listdir(BOFS)):
        if not fn.endswith(".c") or (only and only not in fn):
            continue
        for var, ln, verdict, why in analyze(os.path.join(BOFS, fn)):
            print("  [%s] %-24s %-14s %-8s %s" %
                  ("PASS" if verdict == "SAFE" else "FAIL", fn, var, ln, why))
            if verdict == "SAFE":
                n_safe += 1
            else:
                n_bare += 1
                bare.append("%s:%s" % (fn, var))
    print("\nSAFE=%d BARE=%d" % (n_safe, n_bare))
    if n_bare:
        print("RESULT: FAIL  裸用站点：%s" % ", ".join(bare))
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
