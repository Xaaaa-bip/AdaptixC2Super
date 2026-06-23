#pragma once

#include <windows.h>
#include <winhttp.h>
#include "Connector.h"
#include "ConnectorHTTP.h" // We can reuse HTTPFUNC and ProfileHTTP since WS is upgraded HTTP

// WinHTTP WebSocket APIs are available in Windows 8 and later.
typedef HINTERNET(WINAPI* WinHttpWebSocketCompleteUpgrade_t)(HINTERNET, DWORD_PTR);
typedef DWORD(WINAPI* WinHttpWebSocketSend_t)(HINTERNET, WINHTTP_WEB_SOCKET_BUFFER_TYPE, PVOID, DWORD);
typedef DWORD(WINAPI* WinHttpWebSocketReceive_t)(HINTERNET, PVOID, DWORD, DWORD*, WINHTTP_WEB_SOCKET_BUFFER_TYPE*);
typedef DWORD(WINAPI* WinHttpWebSocketClose_t)(HINTERNET, USHORT, PVOID, DWORD);

struct WSFUNC {
    HTTPFUNC http;
    WinHttpWebSocketCompleteUpgrade_t WinHttpWebSocketCompleteUpgrade;
    WinHttpWebSocketSend_t WinHttpWebSocketSend;
    WinHttpWebSocketReceive_t WinHttpWebSocketReceive;
    WinHttpWebSocketClose_t WinHttpWebSocketClose;
};

typedef struct _ConnectorWSData {
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

    WSFUNC* functions;

    HINTERNET hInternet;
    HINTERNET hConnect;
    HINTERNET hWebSocket;
    BOOL      isUpgraded;

    ULONG server_index;
} ConnectorWSData;

#ifdef __cplusplus
extern "C" {
#endif

Connector* CreateConnectorWS();

#ifdef __cplusplus
}
#endif
