#include "config.h"

#if !defined(AX_CFG_ENTROPY_U32)
#define AX_CFG_ENTROPY_U32 0u
#endif
/* Opaque per-build .data token from -DAX_CFG_ENTROPY_U32 (varies image hash). */
static volatile unsigned long g_ax_cfg_entropy = (unsigned long)AX_CFG_ENTROPY_U32;

#if defined(BUILD_SVC)
char* getServiceName()
{
	return (char*) "ServiceName";
}
#endif

#if defined(BEACON_HTTP) 
char* getProfile()
{
	return (char*)"";
}

unsigned int getProfileSize()
{
	return 1;
}

#elif defined(BEACON_SMB) 

char* getProfile()
{
	return (char*)"";
}

unsigned int getProfileSize()
{
	return 1;
}

#elif defined(BEACON_TCP) 

char* getProfile()
{
	return (char*)"";
}

unsigned int getProfileSize()
{
	return 1;
}

#elif defined(BEACON_DNS) 

char* getProfile()
{
	return (char*)"";
}

unsigned int getProfileSize()
{
	return 1;
}

#endif

int isIatHidingEnabled()
{
	return 1;
}