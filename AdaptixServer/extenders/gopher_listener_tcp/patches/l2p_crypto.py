#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 缺陷四：把 Go 侧 crypto **统一到 ChaCha20**（用户指示「统一使用 chacha20」）。

    python3 l2p_crypto.py check|apply|verify|revert

★★ 缺陷四（实测坐实，非推理）——**listener 与 agent 的加解密线格式互不兼容**：

  listener（gopher_listener_tcp/pl_transport.go，mtime **2026-09-13 00:18**）
      wire = cnonce(12 明文) || ChaCha20_double(gcm.Seal(nonce, nonce, data))
      该段注释自述 "mirrors the agent's C implementation" ⇒ 09-13 那次统一是照
      **C agent**（linux_agent/src_agent/agent/crypt.c）做的。

  agent（gopher_agent/src_gopher/utils/crypt.go，mtime **2026-07-23 原厂**）
      wire = RC4_double(gcm.Seal(nonce, nonce, data))     ← **无线上 nonce**
  builder（gopher_agent/session_crypto.go）
      GopherSessionEncrypt/Decrypt 同样是 RC4 版

  ⇒ 09-13 的统一**漏掉了 Go 的两处实现**。listener 把 agent 密文的头 12 字节当成
    明文 nonce、两条流整体错位 ⇒ `gcm.Open` 报
    `cipher: message authentication failed` ⇒ `goto ERR` ⇒ 发伪装错误页、**静默**。
    **任何 gopher agent 都注册不上，且与传输层无关**（tcp / libp2p 一样失败）。

  实测证据链（patches/l2p_diag.py 的插桩）：
    [DIAG] ACCEPT name=tcp1 proto=tcp key=00000000000000000000000000000000 remote=127.0.0.1:35258
    [DIAG] ERR    name=tcp1 proto=tcp remote=127.0.0.1:35258 recvLen=0 type=0
                  lastErr=cipher: message authentication failed
    真随机密钥的 tt1 listener **同样失败** ⇒ 排除"密钥是 0"的假设。

★ 为什么选"改 Go 侧"而不是"把 listener 退回 RC4"：
  ① 用户指示统一 ChaCha20；
  ② 09-13 的统一是**有意为之**（RC4 是 AV/EDR 高度标记的原语；ChaCha20 + 每条消息
     独立 cnonce 还消掉了 RC4 流重用）；
  ③ listener 与 C agent 已在 ChaCha20 上，退回去要动两处已验收的实现。

★★ 三处实现必须**同时**是 ChaCha20 —— 本项目已归档的那族地雷：同一份密码学协议在
   多个包里各写一份，只改一处就是**静默失联**（无报错、无日志，只有一份伪装错误页）。
   本脚本的 verify 里有一条**机械等价性判据**：把三个文件里的 4 个核心函数
   （chachaQr / chacha20Block / ChaCha20Xor / transportOverlay）抽出来，去掉
   listener/gopher 前缀与注释、压掉空白后**逐字符比对**，三者必须完全一致。

★ 目标文件（payload 在 patches/payload/，用 .go.txt 后缀以免被 `go build ./...` 收进去）：
    crypt_chacha20.go.txt          → src_gopher/utils/crypt.go（agent 侧，**决定性**）
                                     + dist/src_gopher/utils/crypt.go（dist 快照，同 md5）
                                     + 探针 rig 的 src_gopher/utils/crypt.go（端到端验收）
    session_crypto_chacha20.go.txt → session_crypto.go（builder 侧，决定 profile blob）
  ★ **运行态部署树（/root/cc/run/...）本脚本只报告、不落盘** —— 那里 teamserver 正在跑，
    单改 src_gopher 而 .so 未重编 ⇒ 新生成的 agent 用 ChaCha20 而 builder 还是 RC4
    ⇒ 反而把现在能用的 blob 路径也弄坏。部署必须 .so + 源码**同时**更新（见 DEFER 清单）。

★ 没有 git：`.bak-chacha` + 本脚本是唯一回滚手段，REVERT 段与 APPLY 段写在一起。
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys

LST = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp/pl_transport.go"
AGT = "/root/cc/AdaptixServer/extenders/gopher_agent"

PAY_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "payload")
PAY_CRYPT = os.path.join(PAY_DIR, "crypt_chacha20.go.txt")
PAY_SESS = os.path.join(PAY_DIR, "session_crypto_chacha20.go.txt")

BAK = ".bak-chacha"

# (payload, 目标路径, 是否必需)  —— 必需项缺一即 FAIL
TARGETS = [
    (PAY_CRYPT, os.path.join(AGT, "src_gopher/utils/crypt.go"), True),
    (PAY_CRYPT, os.path.join(AGT, "dist/src_gopher/utils/crypt.go"), True),
    (PAY_SESS, os.path.join(AGT, "session_crypto.go"), True),
    (PAY_CRYPT, "/root/gp5probe/l2prun/extenders/gopher_agent/src_gopher/utils/crypt.go", False),
]

# 运行态部署树：只报告，不落盘（见文件头）
DEFER = [
    "/root/cc/run/extenders/gopher_agent/src_gopher/utils/crypt.go",
    "/root/cc/run/extenders/gopher_agent/src_gopher/utils/crypt.go" + BAK,
]

# 三个文件里 4 个核心函数的"规范名"→ 各文件里的实际名（前缀不同，语义必须一致）
# ★ M3 修复（批次3）：原 CORE 只有 4 个原语（QR / Block / Xor / Overlay）。
#   而**线格式由另外几处决定，一个都没比**：KDF（三处异名）、EncryptData/DecryptData
#   （`data[:12]` 分帧、`gcm.Seal(nonce,nonce,…)` 的 nonce 内嵌）、splitSessionKeyMaterial
#   （16/32 字节切分）。
#   后果（审计 M3）：把 builder 的 `GopherSessionDecrypt`（服务端解密 agent 上行）改错
#   ⇒ profile blob 仍能生成（走 Encrypt）⇒ agent 正常注册、**e2e 3/3 PASS**，
#   但注册之后**所有任务交换静默失败** —— 四件套**没有一条会红**。
#   ⇒ 扩到 8 项，覆盖到"决定线格式"的全部函数。
#   ★ 名字是**实测**的（`grep -n '^func '` 三处源码），不是猜的：
#       agent  = src_gopher/utils/crypt.go
#       builder= session_crypto.go
#       listener= gopher_listener_tcp/pl_transport.go
CORE = {
    "chachaQr":          ("chachaQr", "gopherChachaQr", "chachaQr"),
    "chacha20Block":     ("chacha20Block", "gopherChacha20Block", "chacha20Block"),
    "chacha20Xor":       ("chacha20Xor", "gopherChaCha20Xor", "listenerChaCha20Xor"),
    "transportOverlay":  ("transportOverlay", "gopherTransportOverlay", "listenerTransportOverlay"),
    "deriveSubkey":      ("deriveTransportSubkey", "deriveGopherTransportSubkey", "listenerDeriveSubkey"),
    "splitKeyMaterial":  ("splitSessionKeyMaterial", "splitGopherSessionMaterial", "listenerSplitKey"),
    "EncryptData":       ("EncryptData", "GopherSessionEncrypt", "EncryptData"),
    "DecryptData":       ("DecryptData", "GopherSessionDecrypt", "DecryptData"),
}

# ★ 抽空桩断言（M3 的另一半）：`norm()` 之后，**三个空函数体是三个空串，彼此相等**
#   ⇒ "三处等价"会在**全部退化成空桩**时依然为真（假绿第五形态"两个空串也相等"的
#   三串版）。所以除了"相等"，还要断言"**每处都真的有实现**"。
#
# ★★ 2026-09-16 实测更正：这两张表**第一版是我拍的，全错**。
#   我写 `chachaQr: ["uint32","<<"]` —— 而 `<<` **一处都没有**：QR 用的是
#   `bits.RotateLeft32`。`uint32` 也毫无判别力（它出现在**签名**里 ⇒ 空桩也有）。
#   这正是本项目归档的"**期望值靠估不靠数**"（假绿第七形态）。
#   ⇒ 现在的每一个值都是**实测**出来的：先把三处的 body 规范化打出来看，
#     再挑"**真的在算法里、空桩不可能有**"的 token（见下表右列出处）。
#   并且 `stubtest` 模式会证明**这些判据能红**（拿真身换成空桩必须报 BAD）。
#
# 阈值 = 实测 body-only norm 长度 ÷ 3（余量足够，又远高于空桩的 0~13）。
STUB_MIN = {                       # 实测长度：154/755/416/162/80/187/530/465
    "chachaQr": 50, "chacha20Block": 250, "chacha20Xor": 130, "transportOverlay": 50,
    "deriveSubkey": 25, "splitKeyMaterial": 60, "EncryptData": 170, "DecryptData": 150,
}
# token 一律取自 **body-only norm 文本**（不含签名 ⇒ 空桩拿不到）。
# ★ 每个 token 后面括号里是**实测出处**，改代码时若这些 token 消失，先确认是不是真退化。
STUB_TOKENS = {
    "chachaQr":         ["bits.rotateleft32", "*a+=*b"],        # QR 的四条旋转/加异或
    "chacha20Block":    ["littleendian.putuint32", "0x61707865"],  # sigma 常量字面量
    "chacha20Xor":      ["littleendian.uint32"],                # 64 字节块序列化
    "transportOverlay": ["derivetransportsubkey(", "chacha20xor("],  # 组合式实现，无数字/方法
    "deriveSubkey":     ["%16]", "^sk["],                       # out[i]=sk[i]^sk[(i+1)%16]^salt[i]
    "splitKeyMaterial": ["key[:16]", "key[16:32]"],             # ★ 线格式本身：16/32 切分
    "EncryptData":      ["aes.newcipher(", "gcm.noncesize()", ".seal("],
    "DecryptData":      ["cipher.newgcm(", "gcm.noncesize()", ".open("],
}

GOFMT = None
for _c in ("/usr/local/go/bin/gofmt", "/usr/lib/go/bin/gofmt", "/usr/lib/go-1.25/bin/gofmt"):
    if os.path.exists(_c):
        GOFMT = _c
        break


def rd(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def wr(p, t):
    with open(p, "w", encoding="utf-8") as f:
        f.write(t)


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def classify(dst, pay):
    """old / done / missing / unknown —— 绝不把"看不出来"当成 old 或 done。"""
    if not os.path.exists(dst):
        return "missing"
    if md5(dst) == md5(pay):
        return "done"
    t = rd(dst)
    if "crypto/rc4" in t or "rc4Stream" in t or "RC4Stream" in t:
        return "old"
    return "unknown"


def func_body(text, name):
    """抽 `func <name>(` 到下一处顶格 `}` 的整段（本文件的函数都没有嵌套顶格括号）。"""
    i = text.find("func %s(" % name)
    if i < 0:
        return None
    j = text.find("\n}\n", i)
    if j < 0:
        return None
    return text[i:j + 3]


def norm(body):
    """去注释、去 listener/gopher 前缀、压空白、统一小写 —— 只留结构。"""
    keep = []
    for ln in body.split("\n"):
        s = ln.strip()
        if s.startswith("//"):
            continue
        if "//" in ln:
            ln = ln[:ln.index("//")]
        keep.append(ln)
    t = "\n".join(keep).lower()
    # ★ M3 修复（批次3）：把**函数签名里的名字**统一成 F。
    #   三处对同一语义用了不同名字（`splitSessionKeyMaterial` / `splitGopherSessionMaterial`
    #   / `listenerSplitKey`），而签名是 body 的第一行 ⇒ 不统一就会让"结构相同、只是名字
    #   不同"的三处被判为不等（**假红**）。前缀替换（下面那行）解决不了这一类：
    #   去掉 gopher/listener 之后 `splitsessionkeymaterial` 与 `splitsessionmaterial`
    #   仍然不同（差一个 key）。
    #   ★ 2026-09-16 修正：原正则 `func[a-z_][a-z0-9_]*\(` **从来没匹配过** ——
    #     小写化之后文本是 `func encryptdata(`（func 与名字之间**有空格**），缺 `\s*`。
    #     它是静默失效的（不报错、只是什么都不做），靠 dump 模式打出
    #     `funcencryptdata(...)` vs `funcsessionencrypt(...)` 才发现。
    t = re.sub(r"func\s+[a-z_][a-z0-9_]*\s*\(", "func f(", t)
    t = t.replace("listener", "").replace("gopher", "")
    t = t.replace("derivesubkey", "derivetransportsubkey")  # listener 侧名字更短
    # ★ 调用点的异名同理：`splitSessionKeyMaterial(` / `splitGopherSessionMaterial(` /
    #   `listenerSplitKey(`。**一次性**正则（re.sub 不会回头再扫替换结果）——
    #   分两次 replace 会把 `splitkeymaterial` 再变成 `splitkeymaterialmaterial`。
    t = re.sub(r"split(?:session)?(?:key)?material|splitkey", "splitkeymaterial", t)
    t = re.sub(r"\s+", "", t)
    return t


# ★ M3 修复的核心：把"逐字符等价"换成"**结构等价**"。
#
# 为什么原来的判据会假红（2026-09-16 实测，三条 BAD 全是假红）：
#   扩到 8 项后立刻报 splitKeyMaterial / EncryptData / DecryptData 三处不等价，
#   逐段看差异，**没有一条影响线格式**：
#     · `errors.New("ciphertext too short")`  vs  `fmt.Errorf("ciphertext too short")`
#     · `"key too short: %d"`  vs  `"session material: need at least 16 bytes, got %d"`
#     · 参数名 `data` vs `plaintext` vs `ciphertext`；局部变量 `ct` vs `ciphertext`
#   ⇒ 原判据测的是"**实现文本逐字符相同**"，而我们要的是"**线格式兼容**"。
#     对前 5 项（QR/Block/Xor/Overlay/KDF）两者恰好重合，因为它们本来就是逐字复制的；
#     新增的编排类函数是各处独立写的，命名不同是**正常**的。
#
# 规范化规则（保留"决定线格式"的一切，抹掉"只是起名"的一切）：
#   · 字符串字面量 → "S"                —— 错误消息不进线格式
#   · `errors.New(` / `fmt.Errorf(` → `err(` —— 同义构造
#   · 标识符 → V，**但白名单保留**：Go 关键字/内置类型/内置函数
#   · **方法名保留**（`.Seal(` / `.NonceSize(`）—— "用错 API"必须仍然可见
#   · **数字字面量保留** —— `12`（cnonce 长度）/ `16`（密钥切分）正是线格式本身，
#     谁把 12 改成 16 必须红（这是 token 化的主要收益）
GO_KEEP = {
    "func", "return", "if", "else", "for", "range", "var", "const", "type", "struct",
    "interface", "map", "chan", "go", "defer", "select", "switch", "case", "default",
    "break", "continue", "goto", "fallthrough", "package", "import",
    "byte", "rune", "string", "bool", "error", "nil", "true", "false",
    "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64",
    "uintptr", "float32", "float64", "complex64", "complex128",
    "make", "len", "cap", "append", "copy", "delete", "panic", "recover", "new", "close",
}


# ★ 形参"名字组共享类型"：Go 允许 `func f(plaintext, key []byte)`，等价于
#   `func f(plaintext []byte, key []byte)`；gofmt **不会**把前者拆开。
#   实测：EncryptData/DecryptData 剩下的那 **6 个字符**差异**正好就是它**
#   （`V[]byte,V[]byte`=15 vs `V,V[]byte`=9，差 6）—— 语义完全相同，是**假红**。
#   ⇒ 统一展开成"每个名字都带类型"的形式。
#   ★ 局限（如实写下来，不装作没有）：`func f([]byte, a, b []byte)` 这种"无名参数 +
#     名字组"混写会被**多展开一层**（3 槽变 3 槽，但丢了名字）。本项目的 8 个函数
#     **没有一个**是这种写法（签名已逐个读过）；真出现时，方向是"假绿"而不是假红，
#     所以这里额外记一条：**改签名后要重跑 dump 看一眼**。
_PARAM_GROUP = re.compile(
    r"(?<![a-z0-9_\]])(V(?:,V)+)"
    r"(\[\]byte|\[\]uint8|byte|uint32|uint64|int32|int64|int|uint8|uint16|string|error|bool)")


def _expand_param_group(m):
    names = m.group(1).split(",")
    return ",".join(n + m.group(2) for n in names)


def norm_tokens(body):
    """结构指纹：命名无关、数值敏感。见上面 GO_KEEP 的说明。"""
    t = norm(body)
    t = re.sub(r'"[^"]*"', '"S"', t)
    t = re.sub(r"(?:errors|fmt)\.(?:new|errorf)\(", "err(", t)
    prot = {}

    def _keep_method(m):
        k = "\x00%d\x00" % len(prot)
        prot[k] = "." + m.group(1) + "("
        return k

    t = re.sub(r"\.([a-z_][a-z0-9_]*)\(", _keep_method, t)
    t = re.sub(r"[a-z_][a-z0-9_]*",
               lambda m: m.group(0) if m.group(0) in GO_KEEP else "V", t)
    for k, v in prot.items():
        t = t.replace(k, v)
    # ★ 顺序陷阱（2026-09-16 踩过一次）：形参展开必须在"标识符→V"**之后**。
    #   放在前面时正则匹配的是 `data,key[]byte`（还没变成 V）⇒ **一次都没匹配上**，
    #   而且不报错 —— 判据继续报 BAD，看的人会以为"有真差异"。加在这里才对。
    t = _PARAM_GROUP.sub(_expand_param_group, t)
    return t


def body_only(full):
    """去掉签名，只留函数体 —— 桩判据必须只看体，否则**签名里的类型 token
    会让空桩也"通过"**（第一版 STUB_TOKENS 里的 `uint32` 就是这么废掉的）。"""
    k = full.index("{")
    return full[k + 1:-2]          # 末尾是 "\n}\n"


def stub_check(canon, tag, body_norm):
    """抽空桩断言（输入是 **body-only** 的 norm 文本）。

    "三处等价"有一个致命盲区：**三处同时退化成空桩时它依然为真**
    （假绿第五形态"两个空串也相等"的三串版）。所以等价之外还要独立断言
    "每一处都真的像一份实现"：最小长度 + 必须出现的算法特征 token。
    """
    problems = []
    lo = STUB_MIN.get(canon, 40)
    if len(body_norm) < lo:
        problems.append("%s 的 %s 函数体规范化后只有 %d 字符（阈值 %d）—— 疑似空桩/被掏空"
                        % (tag, canon, len(body_norm), lo))
    for tok in STUB_TOKENS.get(canon, []):
        if tok not in body_norm:
            problems.append("%s 的 %s 函数体里找不到算法特征 token %r —— 疑似被换成桩实现"
                            % (tag, canon, tok))
    return problems


def stubtest():
    """★ 证明桩判据**能红**（"门要能红也能绿才算门"）。

    做法：把每个函数的**真身**喂给 `stub_check`（必须**无**问题），
    再把三种"退化形态"喂进去（必须**有**问题）：
      ① 完全空体 `{}`
      ② 只留签名里的类型（把体换成 `var _ uint32` 之类）
      ③ 只留 return（`return nil`）
    如果某一步该红没红 ⇒ 那张表对它没有判别力 ⇒ **所有的 [ok] 都不算数**，
    这时必须 FAIL 而不是继续。这正是 depcheck 的 `--selftest` 同款纪律。
    """
    files = [("agent", os.path.join(AGT, "src_gopher/utils/crypt.go")),
             ("builder", os.path.join(AGT, "session_crypto.go")),
             ("listener", LST)]
    idx = {"agent": 0, "builder": 1, "listener": 2}
    fake = {"①空体": "", "②只留类型": "var_uint32", "③只留 return": "returnnil"}
    ok = True
    for canon, names in CORE.items():
        real = {}
        for tag, path in files:
            if not os.path.exists(path):
                print("  [FAIL] stubtest 读不到 %s" % path)
                return False
            real[tag] = norm(body_only(func_body(rd(path), names[idx[tag]])))
        # 绿侧：真身必须**过**
        for tag in ("agent", "builder", "listener"):
            p = stub_check(canon, tag, real[tag])
            if p:
                print("  [FAIL] stubtest 绿侧：%s 的**真身**被判为桩 ⇒ 判据过严（假红）：%s"
                      % (tag, p[0]))
                ok = False
        # 红侧：三种退化形态每一种、每一处都必须**红**
        for label, stub in fake.items():
            for tag, path in files:
                full = func_body(rd(path), names[idx[tag]])
                hollowed = norm(body_only(_splice_body(full, stub)))
                if not stub_check(canon, tag, hollowed):
                    print("  [FAIL] stubtest 红侧：%s 的 %s 换成「%s」后判据**没红**"
                          " ⇒ 这张表对它没有判别力" % (tag, canon, label))
                    ok = False
        if ok:
            print("  [ok  ] %-16s 真身过 / 3 种退化形态（×3 处）全红" % canon)
    # ★ 把第一版为什么废掉**当场演一遍**（不靠注释里的说辞）：
    #   `uint32` 在"含签名的全文"里有、在"body-only"里没有 ⇒ 拿它当桩判据恒真。
    qr = func_body(rd(os.path.join(AGT, "src_gopher/utils/crypt.go")), "chachaQr")
    hollow = _splice_body(qr, "")
    if "uint32" in norm(hollow) and "uint32" not in norm(body_only(hollow)):
        print("  [ok  ] 反证：签名里的 `uint32` 在全文里有、body-only 里没有"
              " ⇒ 第一版拿它当桩判据是**恒真**的（现已改用 body-only + 算法 token）")
    else:
        print("  [FAIL] 预期「签名 token 不进 body-only」不成立 —— 桩判据的立足点没了")
        ok = False
    return ok


# ★ 变异测试表：(说明, 哪个文件, 哪个函数, 原文, 替换, 期望判定, 为什么要这一条)
#   GREEN 侧（必须仍然 ok）证明**不是**"文本相同"那么简单粗暴（即：命名差异不算缺陷）；
#   BAD 侧（必须报不等）证明**真的能抓到线格式被改坏** —— 尤其是 M3 点名的那个场景：
#   "把 builder 的 GopherSessionDecrypt 改错 ⇒ 注册正常、e2e 全绿、任务交换静默失败"。
EQUIV_CASES = [
    ("builder 的 EncryptData 把 cnonce 长度 12 改成 16", "builder", "EncryptData", "12", "16", False,
     "cnonce 长度就在线上 ⇒ 三处不一致必然对端解不开"),
    ("builder 的参数名 plaintext 改成 pt（纯改名）", "builder", "EncryptData", "plaintext", "pt", True,
     "改名不影响线格式 ⇒ 判据**必须**仍然放行，否则就是假红"),
    ("agent 的 splitKeyMaterial 把 key[16:32] 改成 key[16:24]", "agent", "splitKeyMaterial",
     "key[16:32]", "key[16:24]", False, "16/32 切分就是密钥本身"),
    #     ★ 原文必须**照抄实测的大小写**（`gcm.Open(`，不是 `.open(`）——
    #       第一次写小写，用例直接失效（"变异点不在函数里"），而不是判据失效。
    ("listener 的 DecryptData 把 gcm.Open( 改成 gcm.Seal(", "listener", "DecryptData",
     "gcm.Open(", "gcm.Seal(", False,
     "★ 这就是 M3 点名的静默场景：解密侧方法用错 ⇒ 注册正常、e2e 全绿、任务交换静默失败"),
]


def equivtest():
    """证明**等价判据本身能红**（否则 8 个 [ok] 全是空话）。

    做法：在**内存里**给真源码打一个定点变异，喂给 `equivalence()`，核对判定。
    **一个字节都不落盘**（没有 git，树是资产）。
    """
    print("== 等价判据变异测试（equivtest）==")
    real = {"agent": rd(os.path.join(AGT, "src_gopher/utils/crypt.go")),
            "builder": rd(os.path.join(AGT, "session_crypto.go")),
            "listener": rd(LST)}
    # 前置：真身必须判 ok（否则"变异后判 BAD"可能只是因为它本来就 BAD）
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        base_ok = equivalence(dict(real))
    if not base_ok:
        print("  [FAIL] 前置：真身就没过等价判据 ⇒ 变异测试无意义")
        return False
    print("  [ok  ] 前置：真身判 ok")
    ok = True
    for desc, tag, canon, old, new, want, why in EQUIV_CASES:
        idx = {"agent": 0, "builder": 1, "listener": 2}[tag]
        full = func_body(real[tag], CORE[canon][idx])
        if full is None or old not in full:
            print("  [FAIL] 变异点 %r 不在 %s 的 %s 里 ⇒ 这条用例失效（源码变过了？）"
                  % (old, tag, canon))
            ok = False
            continue
        mutated = real[tag].replace(full, full.replace(old, new, 1), 1)
        assert mutated != real[tag], desc
        contents = dict(real)
        contents[tag] = mutated
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            got = equivalence(contents)
        mark = "ok  " if got == want else "FAIL"
        if got != want:
            ok = False
        print("  [%s] %s —— 期望 %s，实测 %s（%s）"
              % (mark, desc, "ok" if want else "不等", "ok" if got else "不等", why))
    return ok


def _splice_body(full, stub):
    """把函数体换成 stub，**保留签名** —— 专门用来验"签名里的 token 不能当判据"。"""
    k = full.index("{")
    return full[:k + 1] + stub + full[-2:]


def equivalence(contents=None):
    """★ 机械判据：三处实现的 8 个核心函数必须**结构等价**（命名无关、数值敏感）。

    两级判据，缺一不可：
      ① stub_check —— 每处都必须真的像一份实现（防"三个空桩相等"）
      ② 结构指纹逐字符相等 —— 防"某处漏改 / gate 用错 / 12 写成 16"

    `contents` 可注入（{tag: 全文}）—— 只为 `equivtest` 的变异测试服务；
    平时传 None 就是读真文件。
    """
    files = [("agent", os.path.join(AGT, "src_gopher/utils/crypt.go")),
             ("builder", os.path.join(AGT, "session_crypto.go")),
             ("listener", LST)]
    bodies, stub_bad, texts = {}, [], {}
    for tag, path in files:
        if contents is not None:
            t = contents[tag]
        elif os.path.exists(path):
            t = rd(path)
        else:
            print("[BAD ] 等价性判据：缺文件 %s" % path)
            return False
        texts[tag] = t
        for canon, names in CORE.items():
            idx = {"agent": 0, "builder": 1, "listener": 2}[tag]
            b = func_body(t, names[idx])
            if b is None:
                print("[BAD ] 等价性判据：%s 里找不到 %s" % (tag, names[idx]))
                return False
            # 桩检查只看**函数体**（签名里的类型 token 会让空桩蒙混过关）
            stub_bad += stub_check(canon, tag, norm(body_only(b)))
            bodies[(canon, tag)] = norm_tokens(b)   # 等价判据看"结构像不像"
    ok = True
    # ★ 顺序：先证明"三处都不是空桩"，再看"三处相等"。
    #   反过来会先报"等价"再报"没实现"，红的说辞会误导定位。
    for p in stub_bad:
        print("[BAD ] 抽空桩: %s" % p)
        ok = False
    for canon in CORE:
        a, b, l = (bodies[(canon, "agent")], bodies[(canon, "builder")],
                   bodies[(canon, "listener")])
        same = (a == b == l)
        print("[%s] 结构等价: %-16s (agent=%d builder=%d listener=%d)"
              % ("ok  " if same else "BAD ", canon, len(a), len(b), len(l)))
        if not same:
            ok = False
            # ★ 报差异必须用**规范化前**的文本，否则打出来全是 V，等于没报。
            for tag, _path in files:
                idx = {"agent": 0, "builder": 1, "listener": 2}[tag]
                raw = norm(func_body(texts[tag], CORE[canon][idx]))
                print("      %-9s %s" % (tag, raw[:110]))
            print("      ⇒ 先跑 `l2p_crypto.py dump %s`：判「真不等价」还是「规范化不够」"
                  % canon)
    return ok


def gofmt_check(path, tag):
    if GOFMT is None:
        print("[BAD ] 找不到 gofmt —— 判据不可判，按失败处理")
        return False
    p = subprocess.run([GOFMT, "-l", path], capture_output=True, text=True)
    if p.returncode != 0:
        print("[BAD ] %s gofmt rc=%d %s" % (tag, p.returncode, p.stderr.strip()[:300]))
        return False
    dirty = bool(p.stdout.strip())
    print("[%s] gofmt %s" % ("BAD " if dirty else "ok  ", tag))
    if dirty:
        print("\n".join(subprocess.run([GOFMT, "-d", path], capture_output=True,
                                       text=True).stdout.split("\n")[:15]))
    return not dirty


def report_defer():
    for p in DEFER:
        print("  [defer] %-18s %s" % (os.path.basename(p),
                                      "存在" if os.path.exists(p) else "不存在"))


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    for p in (PAY_CRYPT, PAY_SESS):
        if not os.path.exists(p):
            print("[BAD ] 缺 payload %s" % p)
            return 2

    st = [(pay, dst, req, classify(dst, pay)) for pay, dst, req in TARGETS]
    for pay, dst, req, s in st:
        print("[%-7s] %s%s" % (s, dst, "" if req else "  (可选)"))
    if mode == "check":
        print("\n-- 运行态部署树（本脚本不动，部署时随 .so 一起更新）--")
        report_defer()
        return 0 if all(s in ("old", "done", "missing") or not req for _, _, r, s in st) else 1

    if mode == "apply":
        bad = [d for _, d, r, s in st if s in ("unknown",) or (r and s == "missing")]
        if bad:
            print("[BAD ] 以下目标状态不明/缺失，拒绝落盘：\n  " + "\n  ".join(bad))
            return 1
        for pay, dst, req, s in st:
            if s == "done":
                print("[done] %s" % dst)
                continue
            shutil.copy2(dst, dst + BAK)
            shutil.copyfile(pay, dst)   # payload 逐字节落盘 ⇒ md5 判据可判
            print("[ok  ] 已改（备份 %s）" % (dst + BAK))
        return verify()

    if mode == "revert":
        miss = [d for _, d, r, s in st if s == "done" and not os.path.exists(d + BAK)]
        if miss:
            print("[BAD ] 缺备份，拒绝 revert（不许假装成功）：\n  " + "\n  ".join(miss))
            return 1
        for pay, dst, req, s in st:
            if os.path.exists(dst + BAK):
                shutil.move(dst + BAK, dst)
                print("[ok  ] 已还原 %s" % dst)
        return 0

    if mode == "verify":
        return verify()

    # ★ 诊断模式（批次3 加）：等价性判据一旦报 BAD，**必须先判"真不等价"还是
    #   "我的规范化不够"**。做法就是把三处的规范化文本并排打出来看差异段 ——
    #   凭"两个长度不同"推断不出是哪种（长度差 3 字节既可能是缺一个 `,`，
    #   也可能只是变量名多一个字母）。
    if mode == "dump":
        which = sys.argv[2] if len(sys.argv) > 2 else None
        files = [("agent", os.path.join(AGT, "src_gopher/utils/crypt.go")),
                 ("builder", os.path.join(AGT, "session_crypto.go")),
                 ("listener", LST)]
        idx = {"agent": 0, "builder": 1, "listener": 2}
        import difflib

        def _seg(tag_a, x, tag_b, y):
            if x == y:
                print("    %s == %s    ✓" % (tag_a, tag_b))
                return
            print("    %s != %s   （相似度 %.3f）—— 差异段："
                  % (tag_a, tag_b, difflib.SequenceMatcher(None, x, y).ratio()))
            for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, x, y).get_opcodes():
                if op != "equal":
                    print("      %-8s ...%s..." % (tag_a, x[max(0, i1 - 24):i2 + 24]))
                    print("      %-8s ...%s..." % (tag_b, y[max(0, j1 - 24):j2 + 24]))

        for canon, names in CORE.items():
            if which and which != canon:
                continue
            nb, nt = {}, {}
            for tag, path in files:
                body = func_body(rd(path), names[idx[tag]])
                nb[tag] = norm(body) if body else None
                nt[tag] = norm_tokens(body) if body else None
            a, b, l = nb["agent"], nb["builder"], nb["listener"]
            ta, tb, tl = nt["agent"], nt["builder"], nt["listener"]
            print("=" * 74)
            print("### %-18s  norm: a=%s b=%s l=%s   tokens: a=%s b=%s l=%s"
                  % (canon, len(a or ""), len(b or ""), len(l or ""),
                     len(ta or ""), len(tb or ""), len(tl or "")))
            # ★ 结论行放在最前：这是本模式存在的理由 —— 一眼分清
            #   "真不等价"（tokens 也不等，必须改代码）与
            #   "只是命名/文案不同"（tokens 相等，是**判据过严的假红**，不许改代码）。
            if ta == tb == tl:
                print("    ⇒ 结构等价（tokens 三处相同）"
                      + ("；norm 不等只是命名/错误文案差异，**不是缺陷**"
                         if not (a == b == l) else ""))
            else:
                print("    ⇒ ★ 结构不等价（tokens 不同）—— 这是**真差异**，要改的是代码")
            print("  -- norm（含命名，仅供人看）--")
            _seg("agent", a, "listener", l)
            _seg("agent", a, "builder", b)
            print("  -- norm_tokens（判据实际比的就是这个）--")
            _seg("agent", ta, "listener", tl)
            _seg("agent", ta, "builder", tb)
        return 0

    if mode == "stubtest":
        return 0 if stubtest() else 1
    if mode == "equivtest":
        return 0 if equivtest() else 1

    print("用法: %s check|apply|verify|revert|dump|stubtest|equivtest [函数名]" % sys.argv[0])
    return 2


def verify():
    checks = []
    # ★ 前置：桩判据先自证能红。它红不了 ⇒ 下面的 [ok] 全是空话，直接 FAIL。
    #   同款纪律见 l2p_depcheck.py 的 --selftest。
    print("== 判据自检（stubtest + equivtest）==")
    if not stubtest():
        print("\nverify: FAIL（桩判据本身不可信，其余结论一律不算数）")
        return 1
    if not equivtest():
        print("\nverify: FAIL（等价判据本身不可信 —— 它对已知的线格式变异不红，"
              "或对纯改名假红，其余结论一律不算数）")
        return 1
    for pay, dst, req in TARGETS:
        if not os.path.exists(dst):
            checks.append(("目标存在: %s" % dst, not req))
            continue
        t = rd(dst)
        tag = os.path.basename(os.path.dirname(os.path.dirname(dst))) + "/" + os.path.basename(dst)
        checks += [
            ("%s 与 payload 逐字节相同" % tag, md5(dst) == md5(pay)),
            ("%s 不再 import crypto/rc4" % tag, "crypto/rc4" not in t),
            ("%s 无 rc4Stream/RC4Stream 残留" % tag,
             "rc4Stream" not in t and "RC4Stream" not in t),
            ("%s 有 ChaCha20 核心" % tag,
             "chacha20block" in t.lower() and "chachaqr" in t.lower()),
            ("%s 线上格式带 12 字节 cnonce 前缀" % tag, "12+len(out)" in t),
            ("%s 大括号平衡" % tag, t.count("{") == t.count("}")),
            ("%s 无连续空行" % tag, "\n\n\n" not in t),
        ]
    for nm, ok in checks:
        print("[%s] %s" % ("ok  " if ok else "BAD ", nm))

    ok_eq = equivalence()
    # 只有真被改过的文件才做 gofmt（可选目标 missing 时跳过）
    gof = True
    seen = set()
    for _, dst, req in TARGETS:
        if os.path.exists(dst) and dst not in seen:
            seen.add(dst)
            gof = gofmt_check(dst, os.path.basename(dst)) and gof
    # listener 侧必须**没被动过**（本缺陷只改 Go 的两处）
    lst_ok = os.path.exists(LST) and "listenerChaCha20Xor" in rd(LST)
    print("[%s] listener pl_transport.go 仍是 ChaCha20（本脚本不动它）" % ("ok  " if lst_ok else "BAD "))

    bad = sum(1 for _, ok in checks if not ok) + (0 if ok_eq else 1) + \
        (0 if gof else 1) + (0 if lst_ok else 1)
    print("\n-- 运行态部署树 --")
    report_defer()
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


def entry():
    """★ S12 修复：崩溃也必须留下 `RESULT:` 行。

    自动化框架只认 `RESULT:`；崩在中间时留一堆 traceback 而没有 RESULT，
    在汇总里和"这一项没跑到"长得一模一样（"负向观测"家族）。
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
