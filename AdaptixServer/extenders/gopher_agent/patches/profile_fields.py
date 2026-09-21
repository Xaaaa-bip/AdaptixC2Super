#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 §4 步骤 3（**零行为变更**）：给两侧的 Profile 各加 7 个 libp2p 字段。

    python3 patches/profile_fields.py check|apply|verify|revert

★ **只增不改**：新字段**全部追加在末尾** ⇒
  1. msgpack 解码旧 profile 时它们是零值（`Transport == ""` 一律当 `"tcp"`）；
  2. `main.go:24-81` 的 `CreateInfo` 密钥材料拼接顺序**不受影响**（它按既有字段取值，
     不从 struct 反射遍历）；
  3. 两侧（`src_gopher/utils/utils.go` 与 teamserver 的 `pl_utils.go`）**同时改**，
     字段名与 tag **必须逐字一致** —— 否则 msgpack **静默解出零值**（不报错）。

★ 为什么整段替换而不是"插入 7 行"：新字段名 `Libp2pDialTimeout`(17) 比现有的
   `BannerSize`(10) 长 ⇒ gofmt 要求**整块重新对齐**，插行的话会留下一个
   `gofmt -l` 非空的文件。整段替换的代价是动了 10 行**空白**（语义零变化），
   换回来由 `.bak-fields` 保证。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BAK = ".bak-fields"

OLD = '''type Profile struct {
\tType        uint     `msgpack:"type"`
\tAddresses   []string `msgpack:"addresses"`
\tBannerSize  int      `msgpack:"banner_size"`
\tConnTimeout int      `msgpack:"conn_timeout"`
\tConnCount   int      `msgpack:"conn_count"`
\tUseSSL      bool     `msgpack:"use_ssl"`
\tSslCert     []byte   `msgpack:"ssl_cert"`
\tSslKey      []byte   `msgpack:"ssl_key"`
\tCaCert      []byte   `msgpack:"ca_cert"`
}
'''

NEW = '''type Profile struct {
\tType        uint     `msgpack:"type"`
\tAddresses   []string `msgpack:"addresses"`
\tBannerSize  int      `msgpack:"banner_size"`
\tConnTimeout int      `msgpack:"conn_timeout"`
\tConnCount   int      `msgpack:"conn_count"`
\tUseSSL      bool     `msgpack:"use_ssl"`
\tSslCert     []byte   `msgpack:"ssl_cert"`
\tSslKey      []byte   `msgpack:"ssl_key"`
\tCaCert      []byte   `msgpack:"ca_cert"`

\t// ↓ Phase5 新增（**只增不改**，见文件头）。零值语义：Transport == "" ⇒ tcp。
\tTransport         string   `msgpack:"transport"`
\tLibp2pPeerID      string   `msgpack:"libp2p_peer_id"`
\tLibp2pAddrs       []string `msgpack:"libp2p_addrs"`
\tLibp2pRelays      []string `msgpack:"libp2p_relays"`
\tLibp2pPrivKey     []byte   `msgpack:"libp2p_privkey"`
\tLibp2pProtocol    string   `msgpack:"libp2p_protocol"`
\tLibp2pDialTimeout int      `msgpack:"libp2p_dial_timeout"`
}
'''

# 两侧的 Profile 定义。行号只用于"目标是否已达成"的定位，真正的锚点是 OLD 全文。
TARGETS = [
    os.path.join(ROOT, "src_gopher", "utils", "utils.go"),
    os.path.join(ROOT, "pl_utils.go"),
]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    missing = [p for p in TARGETS if not os.path.exists(p)]
    if missing:
        print("[BAD ] 找不到: %s" % missing)
        return 2

    state = {}
    for p in TARGETS:
        t = open(p, encoding="utf-8").read()
        if OLD in t:
            state[p] = "old"
        elif NEW in t:
            state[p] = "done"
        else:
            state[p] = "???"
        print("[%-4s] %-52s %s" % (state[p], os.path.relpath(p, ROOT), ""))

    if mode == "check":
        bad = sum(1 for v in state.values() if v == "???")
        print("\ncheck: %d 处锚点不匹配（old=%d done=%d）"
              % (bad, sum(1 for v in state.values() if v == "old"),
                 sum(1 for v in state.values() if v == "done")))
        return 0 if bad == 0 else 1

    if mode == "apply":
        for p, s in state.items():
            if s == "???":                      # 一处不过 ⇒ 一个字节都不写
                print("[BAD ] %s 锚点不匹配 —— 拒绝落盘" % p)
                return 1
        for p, s in state.items():
            if s == "done":
                print("[done] %s 已是目标态，跳过" % os.path.relpath(p, ROOT))
                continue
            shutil.copy2(p, p + BAK)
            t = open(p, encoding="utf-8").read().replace(OLD, NEW, 1)
            open(p, "w", encoding="utf-8").write(t)
            print("[ok  ] %s 已改（备份 %s）" % (os.path.relpath(p, ROOT), os.path.basename(p) + BAK))
        return verify()

    if mode == "revert":
        for p in TARGETS:
            if not os.path.exists(p + BAK):
                print("[BAD ] 缺 %s.bak-fields —— 拒绝 revert" % os.path.basename(p))
                return 1
        for p in TARGETS:
            shutil.move(p + BAK, p)
            print("[ok  ] %s 已还原" % os.path.relpath(p, ROOT))
        return 0

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


def profile_tags(t):
    """只取 **Profile 块内**的 msgpack tag。

    ★ 不能对整个文件收 tag：`pl_utils.go` 里还有 `SessionInfo` 等同名不同构的结构，
      两侧文件的 tag 全集**本来就不等** ⇒ 那样的判据必然红（"判据自己算错"那一族）。
    """
    i = t.find("type Profile struct {")
    if i < 0:
        return None
    j = t.find("\n}", i)
    return tuple(sorted(
        ln.split('`msgpack:"')[1].split('"')[0]
        for ln in t[i:j].splitlines() if '`msgpack:"' in ln
    ))


def verify():
    """回读目标态：两侧 Profile 的 tag 集合必须**逐字相同**（否则静默解出零值）。"""
    names = {}
    for p in TARGETS:
        got = profile_tags(open(p, encoding="utf-8").read()) or ()
        names[p] = got
        print("[%s] %-52s %d 个 tag"
              % ("ok  " if "libp2p_privkey" in got else "BAD ", os.path.relpath(p, ROOT), len(got)))
    a, b = (names[p] for p in TARGETS)
    same = a == b
    print("[%s] 两侧 tag 集合一致 = %s" % ("ok  " if same else "BAD ", same))
    if not same:
        print("      左独有 %s" % (set(a) - set(b),))
        print("      右独有 %s" % (set(b) - set(a),))
    bad = (0 if "libp2p_privkey" in a else 1) + (0 if same else 1)
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
