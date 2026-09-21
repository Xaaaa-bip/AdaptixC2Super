#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次20 运行态验收总账 —— 对**当前源码构建的**插件、走真 HTTPS。

为什么要有它：单测跑的是 Go 函数，契约脚本跑的是源码文本，
两者都**证明不了**「真实进程 + 真实 TLS + 真实 gcc」这条链是通的。
本脚本把批次20 全部运行态判据收敛成一块板子，一次跑完。
判据一律 fail-closed：拿不到证据就算红，不做「看着差不多」的判断。

用法: b20_bof_runtime_board.py   # 跑完自动清理探针，恢复 26 个模块
环境: B20_RIG=<独立实例根>  B20_PORT=<端口, 默认 39120>
  例: B20_RIG=/root/gp5probe/bofsrig B20_PORT=39120 b20_bof_runtime_board.py
"""
import base64, hashlib, json, os, re, shutil, ssl, subprocess, sys, tempfile
import urllib.request, urllib.error

# ★★ 只许指向**独立验收实例**，绝不指向生产 teamserver。
#   §规则：生产 tree 是只读的；本脚本会**上传并删除** BOF 文件，属于写操作。
RIG = os.environ.get("B20_RIG", "/root/gp5probe/bofsrig")
# 生产 teamserver 的 cwd 是 /root/cc/run —— 显式拒绝，防止手滑把探针灌进生产。
if os.path.realpath(RIG) == "/root/cc/run":
    sys.exit("拒绝：B20_RIG 指向生产 run 树（/root/cc/run）。本脚本只能对独立实例跑。")
EXT = os.path.join(RIG, "extenders", "linux_agent")
OBJ = os.path.join(EXT, "bofs")
SRC = os.path.join(EXT, "src_agent", "bofs")
INC = os.path.join(EXT, "src_agent", "agent")
PROBE = "b20board_probe"

ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
B20_PORT = os.environ.get("B20_PORT", "39120")
BASE = "https://127.0.0.1:%s/api" % B20_PORT

rows, bad = [], 0
def chk(cond, label, extra=""):
    global bad
    if not cond: bad += 1
    rows.append(("✓" if cond else "✗", label, extra))

def call(path, data=None, tok=None, raw=False):
    req = urllib.request.Request(BASE + path, method="POST" if data is not None else "GET")
    req.add_header("Content-Type", "application/json")
    if tok: req.add_header("Authorization", "Bearer " + tok)
    body = json.dumps(data).encode() if data is not None else None
    try:
        with urllib.request.urlopen(req, body, context=ctx, timeout=60) as r:
            b = r.read()
            return r.status, (b if raw else json.loads(b.decode() or "{}"))
    except urllib.error.HTTPError as e:
        b = e.read()
        try:    return e.code, (b if raw else json.loads(b.decode() or "{}"))
        except Exception: return e.code, b

def oobjs():
    """盘上 .o 的独立口径（不经过服务端），用于证伪「服务端说没变」"""
    d = {}
    for f in sorted(os.listdir(OBJ)):
        if f.endswith(".o"):
            d[f] = hashlib.sha256(open(os.path.join(OBJ, f), "rb").read()).hexdigest()
    return d

def gcc_manual(cpath, outdir):
    """authoritative 配方（deploy.sh:63-64），basename 必须与上传件同名"""
    o = os.path.join(outdir, os.path.basename(cpath)[:-2] + ".o")
    r = subprocess.run(["gcc", "-c", "-o", o, cpath, "-include", "bof_api.h", "-Os",
                        "-fPIC", "-fno-stack-protector", "-I", INC],
                       capture_output=True, text=True)
    return r.returncode, o, (r.stderr or "")[:300]

# ── 0 登录（only_password 模式）──────────────────────────────────
import yaml
pwd = yaml.safe_load(open(os.path.join(RIG, "profile.yaml")))["Teamserver"]["password"]
st, lg = call("/login", {"password": pwd})
tok = lg.get("access_token") if isinstance(lg, dict) else None
chk(st == 200 and bool(tok), "登录取得 access_token", f"http={st}")

# ── 1 鉴权门：无 token / 假 token 必须 404（不是 401，默认 404 中间件藏端点存在性）
chk(call("/bofs/list")[0] == 404, "A1 无 token → 404（端点存在性不外泄）")
chk(call("/bofs/list", tok="a.b.c")[0] == 404, "A2 假 token → 404")
chk(call("/bofs/list", tok=tok)[0] == 200, "A3 真 token → 200（前两条才因此有意义）")

# ── 2 列表形状 + 前端契约键 ─────────────────────────────────────
st, L = call("/bofs/list", tok=tok)
chk(st == 200 and L.get("ok") is True, "B1 list 200 且 ok=true")
NEED_E = ["name","obj","obj_size","obj_mtime","obj_sha256","src","src_size","src_mtime","stale"]
miss = [k for k in NEED_E if k not in (L.get("entries") or [{}])[0]]
chk(not miss, "B2 entry 行含前端必读 9 键", f"缺 {miss}" if miss else "9/9")
chk(L.get("count") == len(L.get("entries", [])), "B3 count == len(entries)", str(L.get("count")))
chk(str(L.get("base","")).startswith("/") and L["base"].endswith("/"),
    "B4 base 绝对路径且以 / 收尾（pl_main.go:1595 的 os.Stat 靠它）", L.get("base"))
chk(L.get("n_obj") == L.get("count"), "B5 全部模块都有 .o", f"n_obj={L.get('n_obj')}")
chk(L.get("n_src") == L.get("count"),
    "B6 ★全部模块都有 .c（批次20 新交付：run 树此前没有 src_agent/bofs/）", f"n_src={L.get('n_src')}")
chk(all(os.path.isabs(v) for v in (L.get("dirs") or {}).values()),
    "B7 dirs 三个路径全绝对", str(L.get("dirs")))

# ── 3 重编空转：单编 + 全量，服务端自报与盘上独立口径都要「零字节变化」
before = oobjs()
st, R = call("/bofs/compile", {"name": "host_recon"}, tok=tok)
res = (R or {}).get("result") or {}
chk(st == 200 and res.get("ok") is True, "C1 单编 host_recon 成功", f"http={st}")
chk(res.get("changed") is False, "C2 单编 changed=false（同源同参 ⇒ 无漂移）", str(res.get("changed")))
st, A = call("/bofs/compile", {"all": True}, tok=tok)
chk(st == 200 and A.get("n_changed") == 0,
    "C3 全量重编 n_changed=0", f"n_ok={A.get('n_ok')}/{A.get('count')} n_changed={A.get('n_changed')}")
after = oobjs()
diff = [k for k in before if k in after and before[k] != after[k]]
chk(before.keys() == after.keys() and not diff,
    "C4 ★盘上独立口径复核：26 个 .o 零字节变化（防「服务端自称没变」的自欺）",
    f"变了 {len(diff)} 个" if diff else f"{len(before)} 个全同")

# ── 4 编译失败必须把 gcc 的真话送到操作员眼前（http.ts:92 抛的是 message）
st, E = call("/bofs/upload", {"name": PROBE + "_bad", "kind": "c",
                              "content_b64": base64.b64encode(b"int f(void) { this is not c }\n").decode(),
                              "overwrite": True, "compile": True}, tok=tok)
chk(st == 201, "D1 上传语法错的 .c → 201（传上去了但编译失败）", f"http={st}")
# ★ 上传失败的契约**不是** ok:false 信封 —— 而是 201 + ok:true + compiled:false + compile.*。
#   因为 ok 仍是 true，http.ts:92 不抛，前端走 res.compiled===false 分支读 compile.output
#   （Bofs.tsx:299-303）。所以这里必须按**真实契约**判，不能按我以为的 message 判。
_c = E.get("compile") or {}
chk(E.get("compiled") is False, "D2 上传编译失败 → compiled=false（前端据此走 201 分支）", str(E.get("compiled")))
chk(isinstance(_c.get("error"), str) and _c["error"].strip() != "",
    "D3 ★compile.error 非空（判据非空转：None==None 不算过）", str(_c.get("error"))[:60])
_out = str(_c.get("output") or "")
chk("error" in _out.lower(), "D4 ★gcc 的真实报错在 compile.output 里（操作员看得到）",
    _out.strip().splitlines()[0][:80] if _out.strip() else "output 为空")

# D5：POST /compile 的失败路径**才是** ok:false 信封，那一处必须带 message
st, F = call("/bofs/compile", {"name": "b20_no_such_module"}, tok=tok)
chk(st == 400 and F.get("ok") is False and bool(F.get("message")),
    "D5 /compile 失败 → 400 + ok:false + message（http.ts:92 真正会抛的那条路）",
    f"http={st} msg={str(F.get('message'))[:50]}")

# ── 5 上传 → 编译 → 与权威配方逐字节对照
# ★ 探针源码必须**已知可编译**：第一版是我自己手写的几行，引用了 bof_api.h 里
#   并不存在的 API，于是 E1/E2 判红 —— 那是**我的夹具**错了，不是服务端错了。
#   改直接取现役 host_recon.c 的内容：既保证可编译，又正好用上「.o 字节随源码
#   basename 变」这条铁律（内容同、basename 不同 ⇒ 不能拿 host_recon.o 来对照）。
with open(os.path.join(SRC, "host_recon.c"), "rb") as _f:
    CSRC = _f.read().decode()
st, U = call("/bofs/upload", {"name": PROBE, "kind": "c",
                              "content_b64": base64.b64encode(CSRC.encode()).decode(),
                              "overwrite": True, "compile": True}, tok=tok)
chk(st == 200 and U.get("compiled") is True, "E1 上传合法 .c → 200 且 compiled=true", f"http={st}")
pobj = os.path.join(OBJ, PROBE + ".o")
chk(os.path.isfile(pobj), "E2 产物 .o 落在 bofs/ 下")
if os.path.isfile(pobj):
    got = hashlib.sha256(open(pobj, "rb").read()).hexdigest()
    with tempfile.TemporaryDirectory() as td:
        cp = os.path.join(td, PROBE + ".c"); shutil.copyfile(os.path.join(SRC, PROBE + ".c"), cp)
        rc, mo, err = gcc_manual(cp, td)
        if rc != 0 or not os.path.isfile(mo):
            chk(False, "E3 权威配方手工编译（分母自证）", f"rc={rc} {err}")
        else:
            ref = hashlib.sha256(open(mo, "rb").read()).hexdigest()
            chk(ref == got, "E3 ★上传编译产物 == deploy.sh 权威配方手工编译（逐字节）", ref[:16])

# ── 6 上传的模块必须立刻出现在列表里，且 base+name+.o 能被 pl_main.go:1595 解析
st, L2 = call("/bofs/list", tok=tok)
names = [e["name"] for e in L2.get("entries", [])]
chk(names.count(PROBE) == 1, "F1 新模块出现在列表中且只出现一次（动态识别生效）",
    f"count={L2.get('count')} 命中 {names.count(PROBE)} 次")
p = L2["base"] + PROBE + ".o"
chk(os.path.isfile(p), "F2 base+name+.o 在真实文件系统上可解析", p)

# ── 7 覆盖保护 / ELF 守卫 / 恶意名
chk(call("/bofs/upload", {"name": PROBE, "kind": "c",
                          "content_b64": base64.b64encode(CSRC.encode()).decode()}, tok=tok)[0] == 409,
    "G1 已存在且未 overwrite → 409")
chk(call("/bofs/upload", {"name": "b20notelf", "kind": "o",
                          "content_b64": base64.b64encode(b"not an elf at all").decode(),
                          "overwrite": True}, tok=tok)[0] == 400,
    "G2 非 ELF 的 .o → 400")
badnames = ["../escape", "a/b", "..", ".", "x" * 300]
codes = [call("/bofs/upload", {"name": n, "kind": "c",
                               "content_b64": base64.b64encode(b"int x;\n").decode(),
                               "overwrite": True}, tok=tok)[0] for n in badnames]
chk(all(c == 400 for c in codes), "G3 5 个路径穿越/超长名全 400", str(codes))

# ── 8 清理探针，恢复 26
for f in (PROBE + ".c", PROBE + ".o", PROBE + "_bad.c", PROBE + "_bad.o"):
    for d in (SRC, OBJ):
        try: os.remove(os.path.join(d, f))
        except OSError: pass
st, L3 = call("/bofs/list", tok=tok)
chk(L3.get("count") == 26, "H1 探针清理后回到 26 个模块", f"count={L3.get('count')}")

# ── 出账
print("批次20 运行态验收总账  ——  插件 %s" % hashlib.md5(
    open(os.path.join(EXT, "agent_linux.so"), "rb").read()).hexdigest())
for m, l, e in rows:
    print("  %s %s%s" % (m, l, ("  | " + e) if e else ""))
print("\n合计 %d/%d 通过" % (len(rows) - bad, len(rows)))
sys.exit(1 if bad else 0)
