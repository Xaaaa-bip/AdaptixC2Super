#!/usr/bin/env python3
"""I 表（`/etc/ld.so.preload` 内容读取对抗）+ D3（`/proc/modules` 模块行隐藏）验收

被测机制一句话：挂 `raw_syscalls/sys_exit` 单点，在 read(2) **返回之后**按 fd 表
改写**用户缓冲** —— 文件本体一个字节都不动，只有"被跟踪的那次读"看到抹平后的内容。
实现与取舍见 `src_agent/bpf/redact.c` 文件头。

本脚本要证明的**唯一**区分点是 **O_CLOEXEC**：

    ld.so 读 /etc/ld.so.preload  带 O_CLOEXEC → 豁免 → 读到真路径 → 持久化不断
    工具（cat / 自有 os.open）    不带         → 记表 → 读到空格 → 内容消失

两侧**同时**成立才算方案成立；只成立一侧分别对应"压根没生效"和"把自己搞死"。
所以 T1（工具看不见）与 T2a/T2b（加载器仍看见）是**一对**，缺一条都没有意义。

  T0a 基线：/bin/sleep 的 maps 里本来**没有** libm.so.6（否则 T2a 的"有"无意义，ISS-020）
  T0b 对照（加载前）：os.open 读 preload **能**看到 MARKER（观测手段有效）
  T-load  redact 模块已加载（pin=axp15）
  T1  核心：加载后同一次读 → MARKER 行被抹成**等长空格**、DECOY 行原样、总字节数不变
  T2a 豁免：新 exec 的 /bin/sleep maps 里**有** libm.so.6 → 加载器读到真实内容
  T2b 整份：DECOY 的 ld.so 报错出现在 stderr → 加载器读了**整个文件**，不是只读前半
  T2c 双侧：同一进程同一文件，open()（CLOEXEC 豁免）有 MARKER、os.open()（被跟踪）没有
  T3  close 清表 + fd 复用：同一个 fd 号关闭后改开 /proc/modules，必须走 MODULES 分支
      （若 close 没清表，残留的 PRELOAD 记录会让这次读按 preload 的判据去比对 → 模块名抹不掉）
  T4  非目标不误伤：cat /etc/hostname 原样
  T5  文件本体未变：preload 的 md5/size 全程不变（证明是读时改写，不是写文件）
  T6  unload 后复原：os.open 又能读到 MARKER
  T7  回归：beacon 仍通（按 a_task_id 配对，**不用** console_poll 通用等待 —— ISS-046）
  T8  D3 核心：cat /proc/modules 里**最新加载**的模块行被抹平
  T8b D3 选择性：同一份输出里第 2 个模块**仍在**（没把邻行一起吞掉）
  T8c D3 等长：行数与总字节数与豁免读一致（只换内容，不换长度）

为什么挑**第一个**模块做 D3 —— ★ ISS-074：**这里原来写的话是错的，已实测纠正**。
/proc/modules 由 seq_file 生成，一次 read 可能返回整份；而扫描窗口只有**头部**
AX_SCAN 字节。原稿写"内核 `list_add_tail` 保证后加载的模块排在末尾"，
**实测恰好相反：`/proc/modules` 是「新在前」**（`insmod` 一个探针 .ko，它在**第 1 行**）。
所以窗口必须取**头部**、目标必须取**第 1 行**。取反的代价见 ISS-074：`ko_load` 会把
**无辜模块**登记进 hide_names，而尾部窗口在大缓冲读（`cat`/`lsmod`，ret≈4096）下
**扫不到**新模块 —— 而小缓冲读（≤128）不产生偏移、整块被扫到，于是判据**长期假绿**。
"""
import hashlib
import mmap
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10            # pack_load / pack_unload / BPF_OUT
import p03_hide_link_verify as p03            # run_bof：精确参数字节 + 按 a_task_id 等 107

PIN = "axp15"
LISTENER = "TestHTTP"
PRELOAD = "/etc/ld.so.preload"
MODULES = "/proc/modules"
MARKER = "/lib/x86_64-linux-gnu/libm.so.6"    # 真实存在的库：证明"真 preload 了"
DECOY = "/usr/lib/libdecoy-0001.so"           # 不存在：它的 ld.so 报错 = 加载器读了第 2 行
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-56s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


# ── 两种读法：本脚本的"对照组"本体 ──────────────────────────────────────
def read_tracked(path, n=4096):
    """os.open **不带** O_CLOEXEC → redact 记表 → 被改写的那一侧。

    注意不能用内建 open()：Python 3 的文件对象默认带 O_CLOEXEC（PEP 446），
    走的是豁免路径 —— 这正是本机制的一条已知代价（见 doc 归档）。
    """
    fd = os.open(path, os.O_RDONLY)
    try:
        return fd, os.read(fd, n)
    finally:
        os.close(fd)


def read_exempt(path):
    """工具路径（内建 open()）。

    ★ 语义纠正（实测 strace）：内建 open() 与 os.open() **逐字节同形**，都是
    openat(AT_FDCWD, p, O_RDONLY|O_CLOEXEC)。旧判据拿 O_CLOEXEC 当"加载器"的
    指纹，于是**任何** python/coreutils 读法都被误放行 —— 这正是 T1/T2c/T3
    四个 FAIL 的同源根因。新判据（access 探测 + O_CLOEXEC）下本函数**一样会被
    改写**：豁免只属于 ld.so，进程内的用户态读法不可能观察到它。
    """
    with open(path, "rb") as f:
        return f.read()


def read_true_mmap(path):
    """绕过 BPF 的读法：redact 只 hook read(2)，mmap 走页缓存 → 真实字节。

    这是取"文件本体"证据的唯一可靠手法 —— 用 read 去证明"read 被改写时文件
    没变"是循环论证。mmap 读 /proc/* 会 ENODEV（seq_file 不支持），只用于
    真实文件（/etc/ld.so.preload）。
    """
    fd = os.open(path, os.O_RDONLY)
    try:
        m = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
        try:
            return m[:]
        finally:
            m.close()
    finally:
        os.close(fd)


def spawn_maps(argv, wait=0.6):
    """起一个进程 → 读它 /proc/<pid>/maps → 收尸。"""
    p = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(wait)
        with open("/proc/%d/maps" % p.pid) as f:
            return f.read()
    except OSError:
        return ""
    finally:
        p.kill()
        p.wait()


def spawn_stderr(argv):
    """起一个进程 → 收它的 stderr（ld.so 的 preload 报错就打印在这里）。"""
    try:
        r = subprocess.run(argv, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=20)
        return (r.stderr or b"").decode("utf-8", "replace")
    except Exception as e:
        return "exception: %s" % e


def first_modules(k=3):
    """豁免读 /proc/modules → **开头** k 个模块名。
    ★ ISS-074：/proc/modules 是**新在前**（实测），不是旧在前 —— 取尾部会拿到
    **最老**的模块（cryptd 这种引导期就在的），而缓冲区窗口也只扫头部。
    """
    lines = [l for l in read_exempt(MODULES).decode("utf-8", "replace").splitlines() if l.strip()]
    return [l.split()[0] for l in lines[:k]]


def getuid_ok(aid, timeout=45):
    """发一条 getuid 并等 type=107 回执含 uid= —— 按 a_task_id 配对。"""
    try:
        base = p03.max_id(aid)
        e2e.send_raw(aid, "getuid")
        t0 = time.time()
        while time.time() - t0 < timeout:
            for r in p03._rows(aid, base):
                if r.get("type") == 107 and "uid=" in str(r.get("a_text", "")):
                    return True
            time.sleep(1)
    except Exception as e:
        print("[warn] getuid: %s" % e, flush=True)
    return False


def md5(path):
    try:
        return hashlib.md5(read_exempt(path)).hexdigest()
    except OSError:
        return ""


def kill(pid):
    if pid:
        try:
            os.kill(pid, signal.SIGKILL)   # 按 pid 杀；载荷 cmdline 是伪装名，-f 匹配会误伤（ISS-011）
        except OSError:
            pass


def main():
    if os.path.exists(PRELOAD):
        print("[abort] %s 已存在（%r）—— 本脚本会覆盖它，拒绝执行"
              % (PRELOAD, read_exempt(PRELOAD)[:64]), flush=True)
        return 2

    pid0 = a0 = None
    loaded = False
    try:
        # ── 起 agent（必须先于 preload 文件：加载器读 preload 会让**每次 exec**
        #    都多一次 preload，把 e2e 的启动阶段搅浑）──────────────────────
        bin0 = e2e.gen_agent(LISTENER, "p15a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] A0 aid=%s pid=%d" % (a0, pid0), flush=True)

        # ── T0a：基线 —— sleep 本来不链 libm（否则 T2a 的"有 libm"什么也证明不了）──
        base_maps = spawn_maps(["/bin/sleep", "3"])
        rec("T0a 基线：/bin/sleep maps 中无 libm.so.6", "libm.so.6" not in base_maps,
            "maps %d 字节" % len(base_maps))

        # ── 造现场：两行 preload（第 1 行是真实库 = 可证明真 preload 了；
        #    第 2 行不存在 = 用它的报错证明加载器读了**整份**文件）──────────
        mods = first_modules(3)
        MODULES_NAME = mods[0] if mods else ""
        with open(PRELOAD, "w") as f:
            f.write(MARKER + "\n" + DECOY + "\n")
        ref = read_exempt(PRELOAD)                 # 真实字节（豁免读，参照物）
        ref_md5, ref_len = md5(PRELOAD), len(ref)
        print("[env] preload = %d 字节 / %d 行；D3 目标模块 = %s（阳性对照 = %s）"
              % (ref_len, ref.count(b"\n"), MODULES_NAME, mods[1] if len(mods) > 1 else "?"),
              flush=True)

        # ── T0b：阳性对照 —— 未加载 redact 时被跟踪的读能看见 MARKER ────────
        _, pre = read_tracked(PRELOAD)
        rec("T0b 对照：加载前 os.open 读得到 MARKER", MARKER.encode() in pre,
            "%d 字节" % len(pre))

        # ── 加载 redact（hide_names 里放 MARKER + 最后那个模块名）───────────
        names = (MARKER + "\x00" + MODULES_NAME).encode()
        txt = p03.run_bof(a0, "ebpf_load", g10.pack_load(
            "%s/redact.insns.bin" % g10.BPF_OUT, "%s/redact.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "raw_syscalls/sys_exit", names, "", b"", PIN), timeout=90) or ""
        ok_load = ("failed" not in txt) and ("attached" in txt or "cpus=" in txt)
        loaded = ok_load
        rec("T-load redact 模块已加载 (pin=%s)" % PIN, ok_load,
            (txt.strip().splitlines() or [""])[-1][:64])
        if not ok_load:
            print("[abort] redact 未加载（verifier 拒绝？见上一行回执）", flush=True)
            return 1
        time.sleep(1)

        # ── T1：核心 —— 被跟踪的读看不见 MARKER，但 DECOY 原样、总长不变 ────

        _, tr = read_tracked(PRELOAD)
        rec("T1 核心：os.open 读不到 MARKER（行被抹平）", MARKER.encode() not in tr,
            "命中 %d 字节" % len(tr))
        rec("T1b 选择性：同一读里 DECOY 行原样保留", DECOY.encode() in tr)
        rec("T1c 等长：抹成空格而非截断（前 %d 字节全空格 + 总长不变）" % len(MARKER),
            tr[:len(MARKER)] == b" " * len(MARKER) and len(tr) == ref_len,
            "%d/%d 字节" % (len(tr), ref_len))

        # ── T2a：豁免 —— 加载器仍读到第 1 行，所以**真的 preload 了** libm ──
        maps = spawn_maps(["/bin/sleep", "3"])
        rec("T2a 豁免：新 exec 的 maps 里有 libm.so.6（加载器读到真实路径）",
            "libm.so.6" in maps, "maps %d 字节" % len(maps))

        # ── T2b：加载器读的是**整份**文件（第 2 行的报错照常出现）──────────
        err = spawn_stderr(["/bin/sleep", "0"])
        rec("T2b 整份：DECOY 的 ld.so 报错照常出现", DECOY in err and "cannot be preloaded" in err,
            (err.strip().splitlines() or [""])[0][:64])

        # ── T2c：同一进程、同一文件、同一秒 —— 判据双侧同时成立 ─────────────
        ex, tr2 = read_exempt(PRELOAD), read_tracked(PRELOAD)[1]
        rec("T2c 双侧：内建 open() 与 os.open() 都读不到 MARKER（工具路径一视同仁）",
            MARKER.encode() not in ex and MARKER.encode() not in tr2,
            "open()=%s os.open()=%s" % (MARKER.encode() in ex, MARKER.encode() in tr2))

        # ── T3：close 清表 + fd 复用（残留记录会让 MODULES 走错分支）────────
        fd1 = os.open(PRELOAD, os.O_RDONLY)
        d1 = os.read(fd1, 4096)
        os.close(fd1)
        fd2 = os.open(MODULES, os.O_RDONLY)        # 同一进程里应当复用同一个 fd 号
        d2 = os.read(fd2, 8192)
        os.close(fd2)
        rec("T3 close 清表：fd 复用后仍走 MODULES 分支（首个模块被抹）",
            fd2 == fd1 and MODULES_NAME.encode() not in d2 and MARKER.encode() not in d1,
            "fd %d→%d，读到 %d 字节" % (fd1, fd2, len(d2)))

        # ── T8 系列：D3 —— cat /proc/modules（工具路径，被跟踪）─────────────
        try:
            cat_out = subprocess.run(["/usr/bin/cat", MODULES], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, timeout=20).stdout
        except Exception as e:
            cat_out = b"exception: %s" % str(e).encode()
        rec("T8 D3 核心：cat /proc/modules 里最新加载的模块名消失",
            MODULES_NAME.encode() not in cat_out, "目标 = %s" % MODULES_NAME)
        rec("T8b D3 选择性：第 2 个模块仍在",
            len(mods) > 1 and mods[1].encode() in cat_out,
            "对照 = %s" % (mods[1] if len(mods) > 1 else "?"))
        rec("T8c D3 等长：总字节数与行数同豁免读一致",
            len(cat_out) == len(read_exempt(MODULES))
            and cat_out.count(b"\n") == read_exempt(MODULES).count(b"\n"),
            "%d 字节 / %d 行" % (len(cat_out), cat_out.count(b"\n")))

        # ── T4：非目标文件不误伤 ───────────────────────────────────────────
        try:
            host = subprocess.run(["/usr/bin/cat", "/etc/hostname"], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, timeout=20).stdout
        except Exception:
            host = b""
        rec("T4 非目标不误伤：cat /etc/hostname 原样", host == read_exempt("/etc/hostname"),
            host.strip()[:32])

        # ── T5：文件本体一个字节都没动 ─────────────────────────────────────
        _raw_true = read_true_mmap(PRELOAD)
        rec("T5 文件本体未变：mmap 读的 md5/长度与加载前一致（read 改写不落盘）",
            hashlib.md5(_raw_true).hexdigest() == ref_md5
            and len(_raw_true) == ref_len,
            "%s / %d 字节" % (ref_md5[:8], ref_len))

        # ── T6：unload 后复原（证明是 BPF 在读时改写，不是写进文件）─────────
        try:
            p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
        except Exception as e:
            print("[warn] unload: %s" % e, flush=True)
        time.sleep(1)
        after = read_tracked(PRELOAD)[1]
        rec("T6 unload 后复原：os.open 又能读到 MARKER",
            MARKER.encode() in after and len(after) == ref_len, "%d 字节" % len(after))

        # ── T7：回归 —— beacon 仍通 ────────────────────────────────────────
        rec("T7 回归：beacon 仍通（getuid 回执）", getuid_ok(a0))

        return 0 if all(R) else 1
    finally:
        # 清场顺序要紧：**先删 preload**（它对全机每次 exec 都生效，留一秒都是噪声），
        # 再卸模块、杀进程。
        try:
            if loaded and a0:
                p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN), timeout=60)
        except Exception as e:
            print("[warn] unload(final): %s" % e, flush=True)
        for p in (PRELOAD,):
            try:
                os.remove(p)
            except OSError:
                pass
        print("[env] %s 已清除 = %s" % (PRELOAD, not os.path.exists(PRELOAD)), flush=True)
        kill(pid0)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)


if __name__ == "__main__":
    sys.exit(main())
