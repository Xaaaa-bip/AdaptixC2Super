#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 §4 步骤 4：把 libp2p 分支（build tag 隔离）落进 `src_gopher/`。

    python3 patches/libp2p_branch.py check|apply|verify|revert

本脚本做两件事：
  1. 把 `patches/transport_libp2p.go` 与 `patches/transport_nolibp2p.go` 复制进 `src_gopher/`
     （两个文件各带 `//go:build libp2p` / `//go:build !libp2p`，所以**默认构建不受影响**）；
  2. 校验 `transport.go` 里的分派已经就位（那 3 行由 `seam_transport.py` 生成）。

★ **不碰 go.mod / go.sum** —— 依赖由 `go build -tags libp2p -mod=mod` 自己补齐（见
  `apply` 末尾打印的命令）。**刻意不用 `go mod tidy`**：它会连测试依赖一起拉
  （挂在 quic-go→ginkgo 上），是归档里记过的坑。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(os.path.dirname(HERE), "src_gopher")
NEW_FILES = ("transport_libp2p.go", "transport_nolibp2p.go")
DISPATCH = '\tif profile.Transport == "libp2p" {\n\t\treturn dialLibp2p(profile, addr)\n\t}\n'


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"

    if mode == "revert":
        for f in NEW_FILES:
            p = os.path.join(SRC_DIR, f)
            if os.path.exists(p):
                os.remove(p)
                print("[ok  ] 已删 %s" % f)
        print("       （go.mod / go.sum 的依赖条目需手工回退；或直接删掉 require 块里的 libp2p 行）")
        return 0

    tp = os.path.join(SRC_DIR, "transport.go")
    if not os.path.exists(tp):
        print("[BAD ] 缺 transport.go —— 先跑 seam_transport.py apply")
        return 2
    t = open(tp, encoding="utf-8").read()
    has_dispatch = DISPATCH in t
    print("[%s] transport.go 分派就位" % ("ok  " if has_dispatch else "BAD "))

    got = {}
    for f in NEW_FILES:
        src = os.path.join(HERE, f)
        dst = os.path.join(SRC_DIR, f)
        got[f] = ("src-missing" if not os.path.exists(src) else
                  "done" if os.path.exists(dst) else "old")
        print("[%-11s] %s" % (got[f], f))

    if mode == "check":
        bad = (0 if has_dispatch else 1) + sum(1 for v in got.values() if v == "src-missing")
        print("\ncheck: %d 项不符" % bad)
        return 0 if bad == 0 else 1

    if mode == "apply":
        if not has_dispatch:
            print("[BAD ] transport.go 分派缺失 —— 拒绝落盘（先跑 seam）")
            return 1
        for f in NEW_FILES:
            if got[f] == "src-missing":
                print("[BAD ] 缺 patches/%s —— 拒绝落盘" % f)
                return 1
        for f in NEW_FILES:
            if got[f] == "done":
                print("[done] %s 已存在，跳过" % f)
                continue
            shutil.copy2(os.path.join(HERE, f), os.path.join(SRC_DIR, f))
            print("[ok  ] 已写入 src_gopher/%s" % f)
        print("""
下一步（补齐 go.mod / go.sum 并验证可离线构建）：
  cd src_gopher
  GOWORK=off GOTOOLCHAIN=local GOMAXPROCS=2 CGO_ENABLED=0 \\
    GOMODCACHE=/root/gp5mod GOCACHE=/root/gp5cache \\
    TMPDIR=<scratch> GOTMPDIR=<scratch> \\
    go get github.com/libp2p/go-libp2p@v0.36.5          # ★ 必须显式 pin
  ... GOFLAGS=-mod=mod GOPROXY=off go build -tags libp2p -trimpath -ldflags="-s -w" -o <scratch>/x .

★★ 三个**必须**，每个都是实测踩出来的：
  1. **必须 pin `@v0.36.5`**：不 pin 会解析到 `go-libp2p v0.49.0` ⇒ 它要求
     `go >= 1.25.7` ⇒ 在 `GOTOOLCHAIN=local` 下**直接失败**（本机 go 1.25.4）；
     不设 local 则会**静默下载 go1.26.8** 换掉工具链。
  2. **必须 `CGO_ENABLED=0`**：agent 的真实构建（`pl_main.go:519`）就是 0；漏掉它会
     把 `net` 的 cgo DNS 解析器编进去 ⇒ 产物**凭空大 49,260 字节**，于是任何
     "改造前后体积对照"都会得出错误结论（实测：6,476,068 vs 6,426,808）。
  3. **不要用 `go mod tidy`**：它会连测试依赖一起拉（挂在 quic-go→ginkgo 上）。
     用 `go get <pkg>@<ver>` + `-mod=mod` 构建即可补全 go.mod/go.sum。""")
        return verify()

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


def verify():
    checks = []
    for f in NEW_FILES:
        p = os.path.join(SRC_DIR, f)
        ok = os.path.exists(p)
        tag = ""
        if ok:
            head = open(p, encoding="utf-8").readline().strip()
            tag = head
            ok = head.startswith("//go:build")
        checks.append(("%s 存在且首行是 build 约束（%s）" % (f, tag or "无"), ok))
    tp = os.path.join(SRC_DIR, "transport.go")
    tt = open(tp, encoding="utf-8").read() if os.path.exists(tp) else ""
    checks.append(("transport.go 分派就位", DISPATCH in tt))
    # ★ 关键不变量：默认（无 tag）构建**不得**出现 libp2p 的 import
    lib_imports = sum(1 for f in NEW_FILES if "libp2p" in open(
        os.path.join(SRC_DIR, f), encoding="utf-8").read().split("//go:build")[0])
    checks.append(("两个新文件的 build 约束之前不含 libp2p 字样", lib_imports == 0))
    for n, ok in checks:
        print("[%s] %s" % ("ok  " if ok else "BAD ", n))
    bad = sum(1 for _, ok in checks if not ok)
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
