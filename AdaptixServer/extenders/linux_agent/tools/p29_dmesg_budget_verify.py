#!/usr/bin/env python3
"""P9-9.5 / ISS-075 ①：加载一次 eBPF 产生多少行 dmesg（**预算判据 + 假设判别器**）。

**为什么要这条判据**（ISS-075 原文）：
  > ②**新增判据**：断言"**加载一次 eBPF 产生多少行 dmesg**"的**预算**
  >   （否则这类回归永远无声）

背景：每次加载含 `bpf_probe_write_user` 的 BPF 程序，内核都往 ring buffer 写
`<comm>[<pid>] is installing a program with bpf_probe_write_user helper that may
corrupt user memory!`。**行首就是这个进程的 comm 和 pid** —— B9 的伪装名
（`ksoftirqd/0` / `kworker/R-rcu_` / `ksmd` …）被原样记录，而"一个 `ksoftirqd`
连续装 9 次 BPF 程序"比真名更刺眼（真内核线程**绝不**装 BPF）。

**已知成因假设**：`redact.c` 只有 **1 个** `bpf_probe_write_user` 调用点（`ax_scan`
里），而单次加载记 **9–10 行** ⇒ 怀疑内核是按 **verifier 访问该调用点的状态数**
打印（循环被多状态展开），**不是**按源码调用点数。

★★★ 本脚本的关键设计 = **给这个假设配一个判别器，而不是直接去改代码**：
  `bpf_filter.c` 也有 `bpf_probe_write_user`（`:72`），但**是直线代码、无循环**。
  两者**源码调用点数相同（都是 1）**，差别只在"有没有被循环展开"。
  ⇒ 若 `Δ_filter ≈ 1` 而 `Δ_redact ≈ 9`，假设**成立**（差异只能是状态数带来的）；
     若两者相同，假设**被证伪**，那 9.5-① 的"改整块写回"就**不该做**
     （省下的不是行数，白冒一次 verifier 风险）。

★ 阳性对照（缺了它整条判据作废）：两次加载**都必须真的成功**（`attached`/`cpus=`），
  且卸载后目标词**必须又读得到**。否则"dmesg 没涨"可以廉价地由"根本没加载成功"
  满足 —— 那正是 ISS-083 那条纪律的形态。

★★ 2026-09-16 据实补记（**结论被实测推翻，判据保留、叙述更正**）：
  这条判据原本是为了决定"9.5-① 该不该做（把写回提出循环）"而立的，B2 通过
  时脚本会打印"有行数收益，值得冒一次 verifier 风险"。**实测把这个结论推翻了**：
  把写回目标从**用户指针**换成**栈副本**（偏移随 p 变）⇒ 循环头状态永不合并
  ⇒ `Processed 1000001 insn` ⇒ **9.5-① 判定不做**。
  而同一次查证还发现原推论**误读了那 9 行**：它来自
  `bpf_get_probe_write_proto()` 里的 `pr_warn_ratelimited`，那 9/10 是
  **限速的突发上限**（同窗口 `callbacks suppressed 10718` ⇒ 真实次数远大于 10），
  **不是** verifier 状态数本身。⇒ 判据本身仍然有价值（它测"这行文案的 dmesg
  预算有没有变"），但**不能再拿它推"改代码能省行数"** —— 收益为零：调用点与
  `taint` 位都在，检测面一个字节没变。
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10
import p03_hide_link_verify as p03
import p15_redact_verify as p15

PINR, PINF = "axp29r", "axp29f"
LISTENER = "TestHTTP"
MARKER = "AXP29MARKER"
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def skip(item, note=""):
    """**不可测** —— 既不是通过也不是失败（ISS-094）。

    ★ 只允许用在「可观测量本身失效」的场合，且必须写明理由：拿 SKIP
      掩盖失败等于把门拆了。故收尾处还有一条硬约束：**SKIP ≥ 3 时整项判 FAIL**（没结论）。
    """
    R.append(None)
    print("  [SKIP] %s   %s" % (item, note), flush=True)


def sh(c):
    return subprocess.run(c, shell=True, stdout=subprocess.PIPE).stdout.decode(
        "utf-8", "replace")


def nlines():
    """dmesg 里那类警告的总行数。★ 读不到时返回 -1，绝不返回 0 ——
    '0 行' 与 '读不到 dmesg' 逐字节不同形，混起来判据就没了。"""
    out = sh("dmesg 2>/dev/null | grep -c 'bpf_probe_write_user'").strip()
    try:
        return int(out)
    except ValueError:
        return -1


def clear_log():
    """清空内核环形缓冲 —— **测量前的必需步骤**（ISS-094）。

    ★ 为什么必须清：dmesg 环是**有界**的，满时新行会挤掉旧行 ⇒ `grep -c`
      的**行数差恒为 ~0**，实测还出现过 **-1**。也就是说，在环满的机器上
      "加载前/后各数一次再相减"这个做法**从根上就不成立**：被测量的从来
      不是"新增了几行"，而是"环里恰好还剩几行"。清环之后 Δ 才等于真实新增，
      且非负、单调。
    """
    try:
        subprocess.run("dmesg -C", shell=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15)
    except Exception:
        pass


def nsuppressed():
    out = sh("dmesg 2>/dev/null | grep -c 'callbacks suppressed'").strip()
    try:
        return int(out)
    except ValueError:
        return -1


def settle(sec=6.0):
    """等 ratelimit 窗口过去再测量（入批必需）。

    ★ ratelimit 状态是**按调用点**的：内核里这行只有
      `bpf_get_probe_write_proto()` 一个调用点 ⇒ **所有** BPF 程序共用同一个
      10 行 / 5 秒的桶。单跑时桶是满的，所以 Δ_redact=10 / Δ_filter=2 可复现；
      入批后它紧跟在 p15 / p24 / p27（都加载 redact）后面 ⇒ 桶是空的 ⇒
      Δ 被限流吃掉 ⇒ 判别器 `d_r > d_f` 会**为错误的理由**通过或失败。
      `___ratelimit()` 的窗口自 `rs->begin` 起算 interval(5s) 后复位，
      故 sleep 6s 后**保证**是一个满桶。
    """
    time.sleep(sec)


def load(a0, mod, pin, attach, names):
    txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
        "%s/%s.insns.bin" % (g10.BPF_OUT, mod),
        "%s/%s.mapreloc.txt" % (g10.BPF_OUT, mod),
        5, 1, attach, names, "", b"", pin), timeout=90) or ""
    ok = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
    return ok, (txt.strip().splitlines() or [""])[-1][:70]


def unload(a0, pin):
    try:
        p03.run_bof(a0, "ebpf_unload", g10.pack_unload(pin), timeout=60)
    except Exception as e:
        print("[warn] unload %s: %s" % (pin, e), flush=True)
    time.sleep(0.8)


def main():
    pid0, a0 = 0, None
    loaded = None
    n0 = nlines()
    print("[env] 基线 dmesg 行数=%d（该字符串总行数）" % n0, flush=True)
    if n0 < 0:
        print("[abort] dmesg 读不到 —— 台面不成立，本判据无结论", flush=True)
        return 2
    try:
        bin0 = e2e.gen_agent(LISTENER, "p29a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] aid=%s pid=%d" % (a0, pid0), flush=True)

        # ══ A 相：redact（调用点在 96 次循环 + 32 步展开之内）════════════════
        settle()          # ★ 桶是全局共享的，见函数头注
        n1 = nlines()
        # ── ★★ ISS-094：桶不新 ⇒ 本窗口**不可测**，绝不当 FAIL ─────────
        #    ratelimit 桶被抽干时，加载产生的行**全部**被吞成
        #    `N callbacks suppressed` ⇒ Δ可见行 恒为 0 —— 而这是**内核的状态**、
        #    不是 redact 的属性。故：最多试 3 轮（每轮前 settle 复位桶），
        #    全为 0 ⇒ SKIP（写明理由），而不是红。
        d_r, sup_r, tries = 0, 0, 0
        for attempt in range(3):
            if attempt:
                unload(a0, PINR)
                loaded = None
            # ★★ ISS-094 更根本的一层：dmesg **环形缓冲会翻转**（环满时新行挤掉
            #    旧行）⇒ 行数差恒为 ~0、甚至为负（实测 Δ_filter=-1）。行数差在这
            #    种状态下**根本不是可观测量**。故每次测量前先 `dmesg -C` 清环：
            #    之后的 Δ 才是"真实新增"，且非负、单调、可判别。
            #    顺序要紧：先 settle 让**限速桶**复位（管"会不会被吞"），
            #    再 clear_log 让**环**归零（管"数得准不准"），两件事互不替代。
            settle(8.0)
            clear_log()
            n1 = nlines()
            ok, note = load(a0, "redact", PINR, "raw_syscalls/sys_exit",
                            (MARKER + "\x00").encode())
            if attempt == 0:
                rec("A0 ★★ 阳性对照：redact 真的加载成功（否则下面'没涨'不作数）",
                    ok, note)
                if not ok:
                    return 1
            loaded = PINR
            time.sleep(1)
            n2 = nlines()
            d_r = n2 - n1
            sup_r = nsuppressed() - 0
            tries = attempt + 1
            if d_r >= 1:
                break
        if d_r >= 1:
            rec("A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）",
                True, "Δ=%d 行（%d → %d），第 %d 轮" % (d_r, n1, n2, tries))
        else:
            skip("A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）",
                 "连试 %d 轮 Δ 可见行都是 0 ⇒ **桶不新**（行被限速全吞）⇒ "
                 "本窗口不可测，非产品缺陷（ISS-094）" % tries)
        # ★ 上界同样是要断言的机制：ratelimit 突发上限 10 行/5 秒
        #   ⇒ 单次加载**至多** 10 行。若换个内核、这行不再走 ratelimit，
        #   Δ 会跳到真实状态数（redact 是几十上百）⇒ 这条会红，而那是该知道的事。
        rec("A1b ★ 单次加载 ≤ ratelimit 突发上限 10 行（= 消息确实被限速封顶）",
            d_r <= 10, "Δ=%d 行（>10 = 限速没了，dmesg 的账将按真实状态数爆）" % d_r)

        # 卸载 + 行为复原（阳性对照的另一半）
        unload(a0, PINR)
        loaded = None
        n3 = nlines()
        rec("A2 卸载本身**不**产生新行（卸载路径没有那个 helper）",
            n3 == n2, "%d → %d" % (n2, n3))

        # ══ B 相：bpf_filter（同一个 helper，但**直线代码、无循环**）══════════
        settle()          # ★ 同上：B 相若吃 A 相剩下的桶，判别器就废了
        clear_log()       # ★ 同 A 相：不清环的话行数差被"翻转"吃掉（ISS-094）
        n4 = nlines()
        ok2, note2 = load(a0, "bpf_filter", PINF, "syscalls/sys_enter_bpf", b"")
        rec("B0 ★★ 阳性对照：bpf_filter 真的加载成功", ok2, note2)
        if ok2:
            loaded = PINF
            time.sleep(1)
            n5 = nlines()
            d_f = n5 - n4
            if d_f >= 1:
                rec("B1 bpf_filter 单次加载的 dmesg 行数", True,
                    "Δ=%d 行（%d → %d）" % (d_f, n4, n5))
            else:
                skip("B1 bpf_filter 单次加载的 dmesg 行数",
                     "Δ=0 ⇒ 桶不新，本窗口不可测（ISS-094）")

            # ══ 假设判别 ════════════════════════════════════════════════════
            if d_r < 1:
                skip("B2 ★ 判别器（叙述据实更正）：redact(循环) 的告警行数 **多于** "
                     "bpf_filter(直线)——两者源码调用点数相同（都是 1）",
                     "Δ_redact 不可测（桶不新）⇒ 判别器**无结论**；Δ_filter=%d" % d_f)
            else:
                rec("B2 ★ 判别器（叙述据实更正）：redact(循环) 的告警行数 **多于** "
                    "bpf_filter(直线)——两者源码调用点数相同（都是 1）",
                    d_r > d_f, "Δ_redact=%d vs Δ_filter=%d" % (d_r, d_f))
            # ★★ 以下两行原文写的是「若成立 ⇒ 9.5-① 有行数收益，值得冒一次 verifier
            #    风险」。**实测把这个结论推翻了**（见文件头注 2026-09-16 补记）：
            #    判据保留，但叙述不能停在已知错误的版本上 —— 据实改写。
            print("      ⇒ **正确读法**：Δ_filter=%d 是 bpf_filter 的**真实**告警次数"
                  "（直线代码、无循环展开，次数本来就是小常数）；" % d_f, flush=True)
            print("        Δ_redact=%d 是**限速天花板**（全局共用一个 10 行/5 秒的桶），"
                  "真实次数远大于它（同窗口 suppressed 行可证）。" % d_r, flush=True)
            print("      ⇒ 9.5-①（把写回提出循环）**判定不做**：实测改成栈副本会让循环头"
                  "状态永不合并 ⇒ `Processed 1000001 insn` ⇒ 白冒一次 verifier 风险。",
                  flush=True)
            rec("B3 ★ 报告项：限速计数器行数（反向暴露真实加载次数）",
                True, "suppressed 行=%d；redact Δ=%d / filter Δ=%d"
                      % (sup_r, d_r, d_f))
            unload(a0, PINF)
            loaded = None
        else:
            print("[warn] bpf_filter 没加载成功，判别器无结论（不做'两者相同'的推论）",
                  flush=True)

        rec("C1 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))
    except Exception as e:
        # ★★ 台面自己出事**绝不能**被打成 PASS。汇总原先写在 `finally` 里，于是
        #    崩溃时**仍然打印 `RESULT: PASS`**（只有进程 rc 还是 1）—— 而批次与人
        #    都是读日志里的 `RESULT:`。与 p31 同族（`return` 落在 `finally` 里吞
        #    异常），修法也同 p31：异常在此记一条 FAIL，汇总移到 try/finally 之外。
        rec("C★ 台面跑完（未被异常中断）", False,
            "%s: %s" % (type(e).__name__, str(e)[:70]))
    finally:
        if loaded:
            unload(a0, loaded)
        p15.kill(pid0)
        taint = sh("cat /proc/sys/kernel/tainted").strip()
        print("[env] tainted=%s（本判据不 insmod，故不应由它改变）" % taint,
              flush=True)

    # ── 汇总放在 try/finally **之外** ──────────────────────────────────────
    #    异常不再被吞，也不会在崩溃时打出假的 `RESULT:`。
    ns = sum(1 for x in R if x is None)
    nf = sum(1 for x in R if x is False) + (1 if ns >= 3 else 0)
    if len(R) < 9:          # ★ 条数自证：跑不满就是"判据没跑"，不是"判据通过"
        nf += 1
        print("  [FAIL] D★ 判据自证跑满 9 条（没被静默跳过）   实收 %d 条" % len(R),
              flush=True)
    print("\n[warn] 判据 = %d/%d 通过，%d SKIP%s"
          % (sum(1 for x in R if x), len(R), ns,
             "  ⇒ ★ SKIP≥3 = **本项无结论**，按 FAIL 计" if ns >= 3 else ""),
          flush=True)
    print("RESULT: %s" % ("PASS" if R and nf == 0 else "FAIL"), flush=True)
    return 1 if nf else 0


if __name__ == "__main__":
    sys.exit(main())
