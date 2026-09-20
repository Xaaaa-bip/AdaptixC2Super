/* ISS-087 失败路径自检模块（**用完即删**，不属于产品）。
 *
 * 三个必要条件（缺一条，这个自检就退化成"无判别力的绿"——本文件第一版正是
 * 栽在 ① 之外：注释里写了 `out/` 后跟两个星号加左括号，块注释内因此出现了
 * 注释起始序列 ⇒ `-Werror,-Wcomment` ⇒ 失败发生在**写盘之前** ⇒
 * "产物 md5 没变"变成了"根本没被写，当然没变"）。
 *
 *   ① 过 `clang -Wall -Werror`  —— 否则失败在写盘之前，测不到"还原"；
 *   ② 产出非空 insns.bin       —— 产物**已落盘**（写盘在 verifier 门禁之前）；
 *   ③ verifier **快速**拒绝     —— 见下。
 *
 * ③ 为什么不用"无界循环"（第一版的形状 `for (i=0;i<n;i++)`，n 来自 helper）：
 *   它要走完整的状态爆炸路径才吐 `Processed 1000001 insn` —— 实测在 1965MB 的
 *   小机上跑了 **5 分钟还没返回**。自检要的是"确定、快、可重复"，所以改用
 *   **实参类型错**：`bpf_map_lookup_elem` 的 arg1 是 `ARG_CONST_MAP_PTR`，
 *   喂给它一个 ctx 指针 ⇒ verifier 在**第一条**就拒（`R1 type=ctx
 *   expected=map_ptr`），毫秒级返回，且与内核版本、机器规模无关。
 *
 * ★ 备选形状（万一这个 helper 在某些 libbpf 版本里被改成了 static inline）：
 *   `bpf_get_current_comm((void *)0, 0);` —— arg2 是 ARG_CONST_SIZE，
 *   0 长度 ⇒ verifier 立刻报 invalid size。同样编译期无警告。
 */
#include "common.h"

SEC("tracepoint/syscalls/sys_enter_getuid")
int zz_bad(void *ctx)
{
    /* 故意的：R1 应当是 map 指针，这里喂 ctx ⇒ 立刻被拒 */
    bpf_map_lookup_elem(ctx, ctx);
    return 0;
}
