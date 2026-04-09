#include "Connector.h"

void Connector_DefaultSleep(Connector* self, HANDLE wakeupEvent, ULONG workingSleep, ULONG sleepDelay, ULONG jitter, BOOL hasOutput)
{
	if (!hasOutput)
		WaitMaskWithEvent(wakeupEvent, workingSleep, sleepDelay, jitter);
}

BOOL Connector_DefaultWaitForConnection(Connector* self)
{
	return TRUE;
}

BOOL Connector_DefaultIsConnected(Connector* self)
{
	return TRUE;
}

void Connector_DefaultDisconnect(Connector* self)
{
}
