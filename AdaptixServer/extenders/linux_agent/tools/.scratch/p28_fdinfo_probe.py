#!/usr/bin/env python3
"""P9-9.4 / D4 定性探针 v2：`/proc/<pid>/fdinfo` 是不是 map 可见性的绕过面。

**要判的那句话**（方案 §1.D 第 101 行，原文标 ⚠未验）：
  > `bpf_filter` 挡的是 `bpf()` 系统调用枚举；
  > **直接读 `/sys/fs/bpf` 或 `/proc/<pid>/fdinfo` 是否可绕过** ⚠未验

**v1 的教训（v2 就是为它重写的）**：v1 在**自己进程**里建 map 再读
`/proc/self/fdinfo`，得到"**没有 `map_name`**"。可"内核不印名字"与
"**我的探针根本没把名字设上去**"**逐字节同形** —— 而后者是完全可能的：
`union bpf_attr` 字段错位、`BPF_F_*` 忘了带，都会让名字悄悄不生效。
⇒ v2 补两处，把这条歧义钉死：
  · **Q2c 阳性对照**：同时用 `bpftool map show id N` 读出名字。名字在**对象上**
    确实存在（bpftool 看得见）而 **fdinfo 里没有** —— 这才叫"fdinfo 不印名字"。
  · **Q4 换读者**：在**子进程**里建 map 并持有 fd，**父进程**去读
    `/proc/<子pid>/fdinfo/<fd>` —— 这才是对手的真实姿势（不是自己读自己）。

★ 本探针**不加载任何 rootkit**：它测的是**机制**（内核会不会把 map 名字交给
`/proc/<pid>/fdinfo` 的读者）。机制成立 + 产品代码事实（map 名是 `ax_m0..7`、
BOF 期间经 `OBJ_GET` 拿 fd）⇒ 结论成立。这样判据就与"某次 rootkit 加载"解耦。

★ 判据形态取"**必须看得见**"（阳性）：本探针要证的是**存在**一条读取通道。
"""
import ctypes
import os
import subprocess
import sys
import time

NR_BPF = 321              # x86_64
BPF_MAP_CREATE = 0
MAP_NAME = b"axD4probe"

libc = ctypes.CDLL("libc.so.6", use_errno=True)


class Attr(ctypes.Structure):
    """union bpf_attr 的 MAP_CREATE 分支（按 UAPI 顺序，字段一个不能少）。"""
    _fields_ = [
        ("map_type", ctypes.c_uint), ("key_size", ctypes.c_uint),
        ("value_size", ctypes.c_uint), ("max_entries", ctypes.c_uint),
        ("map_flags", ctypes.c_uint), ("inner_map_fd", ctypes.c_uint),
        ("numa_node", ctypes.c_uint), ("map_name", ctypes.c_char * 16),
        ("map_ifindex", ctypes.c_uint), ("btf_fd", ctypes.c_uint),
        ("btf_key_type_id", ctypes.c_uint), ("btf_value_type_id", ctypes.c_uint),
    ]


R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def mkmap():
    a = Attr()
    a.map_type = 1          # BPF_MAP_TYPE_HASH
    a.key_size = 4
    a.value_size = 4
    a.max_entries = 1
    a.map_name = MAP_NAME
    return libc.syscall(NR_BPF, BPF_MAP_CREATE, ctypes.byref(a), ctypes.sizeof(a))


def child_side(wfd):
    """子进程：建 map、报出 (map_id, fd)、把 fd 持有 12 秒。"""
    fd = mkmap()
    if fd < 0:
        os.write(wfd, b"ERR %d" % ctypes.get_errno())
        os._exit(1)
    mid = 0
    try:
        for line in open("/proc/self/fdinfo/%d" % fd):
            if line.startswith("map_id:"):
                mid = int(line.split()[1])
    except OSError:
        pass
    os.write(wfd, b"%d %d" % (mid, fd))
    os.close(wfd)
    time.sleep(12)
    os._exit(0)


def main():
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        child_side(w)
        os._exit(0)

    os.close(w)
    raw = os.read(r, 128).decode().strip()
    os.close(r)
    if raw.startswith("ERR") or not raw:
        print("[abort] 子进程建 map 失败：%s —— 台面不成立，本探针无结论" % raw,
              flush=True)
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        return 2

    mid, cfd = (int(x) for x in raw.split())
    print("[env] 子进程 pid=%d 持有 map fd=%d（map_id=%d）" % (pid, cfd, mid),
          flush=True)
    time.sleep(0.5)
    try:
        # ── Q1：对手 `ls -l /proc/<pid>/fd` 看到什么 ────────────────────────
        tgt = os.readlink("/proc/%d/fd/%d" % (pid, cfd))
        rec("Q1 对手 `ls -l /proc/<pid>/fd/N` 看到的目标名", True, "%r" % tgt)

        # ── Q4：**跨进程**读 fdinfo（对手的真实姿势）────────────────────────
        p = "/proc/%d/fdinfo/%d" % (pid, cfd)
        txt = open(p).read()
        print("      ── 跨进程 fdinfo 全文 ──", flush=True)
        for line in txt.strip().splitlines():
            print("      %s" % line, flush=True)
        print("      ── 全文完 ──", flush=True)

        # ── Q2c ★★★ 阳性对照：名字在**对象上**确实存在 ────────────────────
        bt = subprocess.run(["bpftool", "map", "show", "id", str(mid)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        bout = bt.stdout.decode("utf-8", "replace")
        name_on_obj = MAP_NAME.decode() in bout
        rec("Q2c ★★★ 阳性对照：`bpftool map show id N` **读得到**这个名字"
            "（证明名字真的挂上了，不是在探针里丢的）", name_on_obj,
            "bpftool 输出含 %r = %s" % (MAP_NAME.decode(), name_on_obj))
        if not name_on_obj:
            print("      bpftool 原文：%s" % bout.strip()[:200], flush=True)

        # ── Q2：判据本身 —— fdinfo 有没有印这个名字 ────────────────────────
        has_name = MAP_NAME.decode() in txt
        rec("Q2 ★★ 跨进程 fdinfo **不印** map 名字（在 Q2c 成立的前提下才作数）",
            not has_name,
            "含名字=%s；含 map_id=%s；含 map_type=%s"
            % (has_name, "map_id" in txt, "map_type" in txt))
    finally:
        os.kill(pid, 9)
        os.waitpid(pid, 0)

    # ── Q3：进程没了之后，通道自然关闭（窗口是**有界**的）──────────────────
    gone = not os.path.exists("/proc/%d/fdinfo/%d" % (pid, cfd))
    rec("Q3 持 fd 的进程一退出，该 fdinfo 路径即消失（窗口有界）", gone,
        "/proc/%d 存在=%s" % (pid, not gone))

    print("\n[warn] 探针 = %d/%d" % (sum(R), len(R)), flush=True)
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
