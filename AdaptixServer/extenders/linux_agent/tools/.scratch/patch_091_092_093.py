#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P9 收口期三连补丁（ISS-091 / ISS-092 / ISS-093）—— **批次结束后**运行。

★ 三条锚点纪律（都是踩出来的，不是想出来的）：
  ① 每处替换必须**恰好命中 1 次**。0 次 = 我记错了原文；≥2 次 = 锚点不够独特。
     第一版就因为 deploy.sh 那行注释的**短横线数量**记错而 0 命中（`────` vs `──────`）。
  ② 同一个文件上的多处替换**必须串行累积**（PEND 字典），不能各自基于原盘内容算替换串。
     第二版把 sub() 改成"先全量校验、最后统一落盘"以求原子性，却让**同一文件的后一处
     覆盖前一处**：deploy.sh 只剩收尾行、ja4_harness.sh 的 4 处只剩最后 1 处。
     ——"原子"和"串行"是两件事，只顾前者会把后者弄丢。
  ③ `--check` 不遇错即退：一次把全部状态报出来，否则第一个坏锚点会遮住其余。

★ 本脚本自带 **REVERT**：把第二版留下的半应用先反解回原样，再做完整应用。
  幂等：干净树上跑 = 8 处应用；半应用树上跑 = 2 处反解 + 8 处应用。

用法: python3 tools/.scratch/patch_091_092_093.py [--check]
"""
import io
import os
import sys

T = "/root/cc/AdaptixServer/extenders/linux_agent"
CHECK = "--check" in sys.argv

PEND = {}           # path -> 该路径**累积后**的待写内容
OPS = []
BAD = []


def sub(path, old, new, tag, optional=False):
    p = os.path.join(T, path)
    if p not in PEND:
        PEND[p] = io.open(p, encoding="utf-8", newline="").read()
    d = PEND[p]
    c = d.count(old)
    if c != 1:
        # ★「已是目标态」与「锚点记错了」必须分开 —— 两者都是 old 命中 0 次：
        #   前者 = new 已就位（幂等重跑的正常情形，见上一版留下的半应用）；
        #   后者 = new 也不在（那才是失败）。
        if d.count(new) >= 1:
            print("[done] %-26s %-22s 已是目标态，跳过" % (tag, path))
            return
        if optional:
            print("[skip] %-26s %-22s 锚点命中 %d 次" % (tag, path, c))
            return
        print("[BAD ] %-26s %-22s 锚点命中 %d 次（要求 1）" % (tag, path, c))
        BAD.append(tag)
        return
    PEND[p] = d.replace(old, new)
    OPS.append(tag)
    print("[ok  ] %-26s %-22s" % (tag, path))


# ══ REVERT：第二版在同一文件上的自我覆盖留下的半应用 ══════════════════════
# （optional=True：干净树上这两处命中 0 次是**正常**的，不算失败）
sub("tools/deploy.sh",
    'echo "[deploy] 无漂移：agent 源码 + mbedtls($(ls "$MBED_SRC"/library/*.c | wc -l) 个 .c) '
    '+ $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF + $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"',
    'echo "[deploy] 无漂移：agent 源码 + $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF '
    '+ $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"',
    "REVERT deploy 收尾行", optional=True)

_AUTO = '''    ;;
auto)
    # 自包含入口：自己找端口 → 抓包 → 复用同一份比较器。
    # 端口从运行期 DB 取（TestWSS 的 port_bind），取不到才退回 8443。
    port="${2:-}"
    secs="${3:-30}"
    if [ -z "$port" ]; then
        port="$(python3 - <<'PYEOF' 2>/dev/null
import json, sqlite3
try:
    c = sqlite3.connect("file:/root/cc/run/data/adaptixserver.db?mode=ro", uri=True, timeout=5)
    r = c.execute("select ListenerConfig from Listeners where ListenerName='TestWSS'").fetchone()
    print(int(json.loads(r[0]).get("port_bind") or 8443) if r else 8443)
except Exception:
    print(8443)
PYEOF
)"
        [ -z "$port" ] && port=8443
    fi
    mkdir -p "$HERE/.scratch"
    out="$HERE/.scratch/ja4_auto_$$.pcap"
    echo "[*] auto: capture ${secs}s on lo port ${port} -> ${out}"
    timeout "${secs}" tshark -i lo -f "tcp port ${port}" -w "$out" >/dev/null 2>&1
    bash "$0" verify "$out"; rc=$?
    if [ "$rc" = 0 ]; then rm -f "$out"; else echo "[i] 留证: $out"; fi
    exit "$rc"
    ;;
*)
    echo "unknown subcommand: $1"
'''
sub("tools/ja4_harness.sh", _AUTO,
    '    ;;\n*)\n    echo "unknown subcommand: $1"\n',
    "REVERT ja4 auto 块", optional=True)

# ══ 正式应用 ═══════════════════════════════════════════════════════════════
# ── ISS-092: deploy.sh 加第四条轴（src_agent/mbedtls/**）───────────────────
# ★ 锚点只取到注释正文、**不含尾部短横线**：那串 `─` 的数量是最容易记错的部分，
#   而前缀（`done` + 空行 + 该注释）已经足够独特。
DEP = "tools/deploy.sh"
sub(DEP,
    'done\n\n# ── 2. BOF：重编全部，逐个对拍 .text 大小',
    '''done

# ── 1b. mbedtls（JA4 书写器所在 —— 第四条构建轴，ISS-092）─────────
# ★ pl_main.go:486 NativeMbedtlsSrcDir="src_agent/mbedtls/library" 与 :499
#   -I src_agent/mbedtls/include -I src_agent/mbedtls
#   -DMBEDTLS_CONFIG_FILE='<mbedtls_config_agent.h>' 全是**相对路径**
#   ⇒ 运行期编的是 $RUN/src_agent/mbedtls/**。
#   它既是编译输入、又不在前三条轴（agent/BOF/BPF）里
#   ⇒ 改了 ssl_client.c 的 MBEDTLS_AX_JA4 块而 --check **假绿**、
#   运行态继续编旧 ClientHello（**静默退化**，最难发现的一类）。
# 判据 = `diff -rq` **逐文件比字节**，不是比 mtime（理由同 2b）。
MBED_SRC="$SRC/src_agent/mbedtls"
MBED_RUN="$RUN/src_agent/mbedtls"
if [ ! -d "$MBED_RUN" ]; then
    say "MISSING src_agent/mbedtls" "run/ 下不存在（生成 agent 会失败）"
    drift=1
    [ $CHECK -eq 0 ] && cp -a "$MBED_SRC" "$MBED_RUN"
elif ! diff -rq "$MBED_SRC" "$MBED_RUN" >/dev/null 2>&1; then
    say "DRIFT src_agent/mbedtls" "$(diff -rq "$MBED_SRC" "$MBED_RUN" 2>/dev/null | wc -l) 个文件差异"
    drift=1
    if [ $CHECK -eq 0 ]; then rm -rf "$MBED_RUN"; cp -a "$MBED_SRC" "$MBED_RUN"; fi
fi

# ── 2. BOF：重编全部，逐个对拍 .text 大小''',
    "ISS-092 §1b mbedtls 轴")

sub(DEP,
    'echo "[deploy] 无漂移：agent 源码 + $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF '
    '+ $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"',
    'echo "[deploy] 无漂移：agent 源码 + mbedtls($(ls "$MBED_SRC"/library/*.c | wc -l) 个 .c) '
    '+ $(ls "$BOF_SRC"/*.c | wc -l) 个 BOF + $(ls "$SRC"/src_agent/bpf/*.c | wc -l) 个 BPF 模块与源码一致"',
    "ISS-092 收尾行补 mbedtls")

# ── ISS-093: ja4_harness.sh 加 auto（自包含）+ capture 落 .scratch ─────────
JH = "tools/ja4_harness.sh"
sub(JH,
    '# Usage:\n#   ja4_harness.sh capture <port> [seconds]   # tshark capture of agent traffic\n'
    '#   ja4_harness.sh verify <pcap>             # compute JA4 + diff vs baseline\n',
    '# Usage:\n#   ja4_harness.sh auto [port] [seconds]     # 自包含：找端口 → 抓包 → 比对（推荐）\n'
    '#   ja4_harness.sh capture <port> [seconds]   # tshark capture of agent traffic\n'
    '#   ja4_harness.sh verify <pcap>             # compute JA4 + diff vs baseline\n'
    '#\n# ★ 用 `auto`，不要无参调用本脚本（ISS-093：gate4.sh 曾经就是\n'
    '#   无参调用 ⇒ 走 usage 分支 exit 2 ⇒ **恒 FAIL**：死门比没门更坏）。\n',
    "ISS-093 usage 头")
sub(JH,
    'if [ "$#" -lt 1 ]; then\n    echo "usage: ja4_harness.sh capture <port> [seconds] | verify <pcap>"\n',
    'if [ "$#" -lt 1 ]; then\n    echo "usage: ja4_harness.sh auto [port] [seconds] | capture <port> [seconds] | verify <pcap>"\n',
    "ISS-093 usage 回显")
sub(JH,
    '    out="/tmp/ja4_capture_$$.pcap"\n',
    '    # ★ 不落 /tmp：避免与 p17 T6 的**时间窗**判据纠缠（同批排在 p17 之后的 /tmp 写会假红）\n'
    '    out="$HERE/.scratch/ja4_capture_$$.pcap"; mkdir -p "$HERE/.scratch"\n',
    "ISS-093 capture 落盘")
sub(JH, '    ;;\n*)\n    echo "unknown subcommand: $1"\n', _AUTO, "ISS-093 auto 子命令")

# ── ISS-093: gate4.sh 不再无参调用（改用 p31：它自带阴性对照）──────────
sub("tools/gate4.sh",
    'run "gap12 JA4 harness"    bash tools/ja4_harness.sh\n',
    'run "gap12 JA4 no-regress" python3 tools/p31_ja4_regress.py\n',
    "ISS-093 gate4 死门")

# ── ISS-091: hide_add.c 注释（能力升级了、边界叙述没跟上）─────────────────
sub("src_agent/bofs/hide_add.c",
    '/// 边界：hide_names 只挡**目录枚举**（getdents64），不挡 open()/read()\n'
    '/// —— `cat /etc/cron.d/<job>` 照样读得到内容（陷阱 1）。\n',
    '/// 边界（2026-09-16 更正，ISS-091）：目录枚举层由 hide_names 过滤\n'
    '/// （getdents64）；**内容层**已由 AX_KIND_HIDE 整块抹平（P9-9.1，\n'
    '/// `cat /etc/cron.d/<job>` 也拿不到）。残留边界只剩：AX_HIDE_CAP=512\n'
    '/// 以上的尾部、以及未纳入 <names> 的路径。\n',
    "ISS-091 注释更正")

# ══ 汇总：全部命中才落盘（同一路径只写一次，写完的是**累积后**的内容）══════
print("\n%d 处待应用%s" % (len(OPS), "（--check，未写入）" if CHECK else ""))
if BAD:
    print("未命中: %s" % ", ".join(BAD))
    print("⇒ 一个字节都没写")
    sys.exit(2)
if not CHECK:
    for p, txt in PEND.items():
        io.open(p, "w", encoding="utf-8", newline="").write(txt)
        print("  written %s" % p)
    print("已写入 %d 个文件" % len(PEND))
