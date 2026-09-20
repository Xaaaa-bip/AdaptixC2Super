#!/usr/bin/env python3
"""零副作用探针：只做 GET_NEXT_ID / GET_FD_BY_ID / close —— 不创建、不 pin、不 attach。
目的：① 核对命令号 12/14 ② **实测** GET_FD_BY_ID 的输入字段落在偏移 0 还是 4
（这一段代码里最容易"编译过、跑起来静默不生效"的地方，与 ISS-064 同族）。

★ 为什么值得单独跑：B10 新通道的失败是**静默**的（mfd<0 → continue，没有回执），
   字段写错位置的表现 = "什么都没发生"，与"这台机器没有 rootkit"完全同形。
"""
import ctypes, os

libc = ctypes.CDLL("libc.so.6", use_errno=True)
libc.syscall.restype = ctypes.c_long          # 不写这行，返回值会被截成 int
NR_BPF = 321                                  # x86_64

def bpf(cmd, buf, size=32):
    ctypes.set_errno(0)
    r = libc.syscall(NR_BPF, cmd, ctypes.byref(buf), size)
    return r, ctypes.get_errno()

print("== ① MAP_GET_NEXT_ID (cmd=12) ==")
a = (ctypes.c_uint32 * 8)()
a[0] = 0                                      # start_id
r, e = bpf(12, a)
print("   start_id=0 -> ret=%s errno=%s  buf[1](next_id)=%s" % (r, e, a[1]))
if r != 0:
    print("   !! 12 号命令不对，或本机无 map")
    raise SystemExit(1)
mid = a[1]

print("== ② MAP_GET_FD_BY_ID (cmd=14) 输入字段位置实测 ==")
for slot in (0, 1):
    b = (ctypes.c_uint32 * 8)()
    b[slot] = mid                             # 分别把 id 放在偏移 0 / 偏移 4
    fd, e2 = bpf(14, b)
    tag = "偏移0(id)  " if slot == 0 else "偏移4(next_id)"
    print("   %s = %-6s -> fd=%-4s errno=%s %s"
          % (tag, mid, fd, e2, "  ← 有效 fd = 输入字段在这" if fd > 0 else ""))
    if fd > 0:
        os.close(fd)

print("== ③ 防误判：命令号 13（PROG_GET_FD_BY_ID）不应返回 map fd ==")
c = (ctypes.c_uint32 * 8)()
c[0] = mid
fd3, e3 = bpf(13, c)
print("   cmd=13 id=%s -> fd=%s errno=%s" % (mid, fd3, e3))
if fd3 > 0:
    os.close(fd3)
print("== 结束（未做任何写操作）==")
