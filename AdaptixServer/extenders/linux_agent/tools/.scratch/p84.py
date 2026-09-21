# -*- coding: utf-8 -*-
"""ISS-084：`/proc/modules` 的**依赖列**把模块名漏在外面（逗号不是词边界）。

★ 实测现场（tools/.scratch/p26.log，探针把两张表都 dump 出来当物证）：

    map0(hide_names) 里有 "tcp_diag"，**逐字节**等于 ax_scan 构造的 64 字节键   ✓
    map7(redact_fds) 里 fd 被记成 kind=1(MODULES)                            ✓
    读回来前 32 字节 = 20×8 + "12288 0 - Live 0xffffff"
                       ^^^^^^ 自己那一行**确实被抹平了**

  可判据 `"tcp_diag" not in 全文` 仍为假。量偏移：

    tcp_diag @ 0      ← 自己那行（已被抹）
    tcp_diag @ 61     ← `inet_diag 28672 1 tcp_diag, Live 0x…` 的**依赖列**

  61 稳稳落在 128 字节窗内、p 循环（0..95）也罩得住 ⇒ **不是窗口、不是登记、
  不是 fd 记账，是词边界**：`ax_scan` 只用 `c < 33`（空白/控制符）判词尾，而依赖列
  的分隔符是 **`,`(44)** ⇒ 提取出的是 `"tcp_diag,"`（9 字节），与登记键不等 ⇒
  查表不中 ⇒ 一个字节都没抹。

★ 为什么这条比"没抹"更严重：自己那行成了 `       12288 0 - Live 0x…`（名字空白），
  而名字**原样躺在别人的依赖列里**。`/proc/modules` 是 rootkit 检测第一现场，
  「自己那行没名字、别人的依赖列里有」比两处都露着更扎眼。

★★ 两次失败留下的判据（比这次成功更值钱）：

  v1：把 `comma = ax_iszero(c ^ 44)` 并进 `end`，让 `run` 在逗号处死掉。
      ⇒ `BPF program is too large. Processed 1000001 insn`。
  v2：改成循环**之后**一趟后处理，读回 `km[j]` 再改。
      ⇒ 同样 1000001，报错里是 `R1_w=scalar(id=98198,…)`。

  两次都撞在同一条上：**`km` 在 map key 的精确链上**（kb ← helper 拷 km ← …），
  verifier 对它的每一位做精确性跟踪。
    · v1 的错在 `run` —— 它是**循环携带**量（跨 32 字节展开、又跨 96 次 p 迭代），
      给它多挂一维精度 ⇒ 循环头状态永不合并；
    · v2 的错在**回读 `km[j]`** —— 栈回读的值 verifier 发**全新唯一 id**，
      写回 km 后每个 p 迭代的头状态都不同 ⇒ 同样不合并。
  ⇒ 判据：**碰 `km` 只能写、不能读；更不能让 `run` 多挂维度。**

★ 本版修法：**只把 `,` 从「键」里剔掉**，一行，值在**寄存器**里从 `c` 现算，
  既不碰 `run`（不携带）、也不回读 `km`。

  ★★ 而 `raw`（抹平的那一侧）**一个字都不用改** —— 这是本次最省的一步：
    `,` = 44 ≥ 33 ⇒ `run` 在逗号处**还活着** ⇒ `mm` 覆盖它 ⇒ 命中时
    `raw[0..8]` 全变空格 ⇒ 实际抹掉的是 `"tcp_diag,"` **整整 9 字节** ⇒
    依赖格成整格空白（`inet_diag 28672 1          Live 0x…`），
    比只抹名字、留下一个孤零零的 `,` 自然得多。

★ 实测支撑（决定了 `name,` 这个形状覆盖得全不全）：`/proc/modules` 的依赖列
  **每一个条目后面都有逗号**，包括最后一个 —— 实测 `tcp_diag,` / `garp,stp,` /
  `raid456,async_raid6_recov,`。所以名字出现在依赖列的**任何位置**，其后必是逗号。

★ 已知代价（明写出来，不装作没有）：若名字是**逗号列表的第一项**，键会收成第一段，
  命中后会把**整段列表**抹成空白（`garp,stp,` 两项一起没）。这是**多抹**，
  方向与失败相反：多抹的是同格里的邻居（该格本来就要变空白），
  而漏抹会直接把我们的名字交出去。取了多抹这一侧。

★ 不修、单独立档的有界缺口：扫描窗只有 128 字节（`tmp[AX_TMP]` 与贴着 BPF 512
  上限的栈绑死）。模块名出现在**偏移 > 96 的依赖列**里时仍会漏 —— 本文件头注那句
  "登记的就是刚加载的那一个 ⇒ 必然落在首窗"只管住了模块**自己那一行**
  （`/proc/modules` 新在前），管不住"依赖它的、更早加载的模块"。

每处替换断言唯一命中，否则不写盘。
"""
import io, os, sys

ROOT = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/"
F = "bpf/redact.c"

OLD = '''            run = run & (1u - end);
            mm  = (unsigned char)(0u - run);
            km[j]  = (unsigned char)(c & mm);
            raw[j] = (unsigned char)((' ' & mm) | (c & (unsigned char)~mm));'''

NEW = '''            run = run & (1u - end);
            mm  = (unsigned char)(0u - run);
            /* ★★ ISS-084：`,`(44) 只从**键**里剔掉。
             * 依赖列的每一项后面都跟着逗号，所以名字提取出来是 `"tcp_diag,"`，
             * 与登记键 `"tcp_diag"` 不等 ⇒ 查表不中。
             * ★ 值在**寄存器**里从 c 现算：`run` 是循环携带量，给它多挂一维精度、
             *   或回读 km[j]（栈回读会被发全新唯一 id），都会让 p 循环头状态
             *   永不合并 —— 两次实测都是 `Processed 1000001 insn`。
             * ★ `raw` 不动：`,` 落在抹平区内（44 >= 33 ⇒ run 在逗号处还活着），
             *   命中时 `"tcp_diag,"` 整 9 字节一起变空格，依赖格自然是整格空白。 */
            cm  = (unsigned char)(0u - ax_iszero((__u32)c ^ 44u));
            km[j]  = (unsigned char)(c & mm & (unsigned char)~cm);
            raw[j] = (unsigned char)((' ' & mm) | (c & (unsigned char)~mm));'''

# 循环体里多一个局部变量，声明要跟着加（与 mm 同一行区域）。
OLD_D = '''            unsigned char c  = raw[j];
            __u32 end = ax_lt((__u32)c, 33u);
            unsigned char mm;'''
NEW_D = '''            unsigned char c  = raw[j];
            __u32 end = ax_lt((__u32)c, 33u);
            unsigned char mm, cm;'''

src = io.open(ROOT + F, encoding="utf-8").read()
for tag, old, new in (("正文", OLD, NEW), ("声明", OLD_D, NEW_D)):
    n = src.count(old)
    print("%s 命中: %d" % (tag, n))
    if n != 1:
        print("ABORT 未命中/不唯一")
        sys.exit(1)
    src = src.replace(old, new)

io.open(ROOT + F, "w", encoding="utf-8").write(src)
print("OK 已写入:", F)
