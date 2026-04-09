#pragma once

#include "std.cpp"
#include "Packer.h"

#define COMMAND_TUNNEL_START_TCP 62
#define COMMAND_TUNNEL_START_UDP 63
#define COMMAND_TUNNEL_WRITE_TCP 64
#define COMMAND_TUNNEL_WRITE_UDP 65
#define COMMAND_TUNNEL_CLOSE     66
#define COMMAND_TUNNEL_REVERSE   67
#define COMMAND_TUNNEL_ACCEPT    68
#define COMMAND_TUNNEL_PAUSE     69
#define COMMAND_TUNNEL_RESUME    70

#define TUNNEL_CREATE_SUCCESS 0
#define TUNNEL_CREATE_ERROR 1

#define TUNNEL_STATE_CLOSE   0
#define TUNNEL_STATE_READY   1
#define TUNNEL_STATE_CONNECT 2

#define TUNNEL_MODE_SEND_TCP 0
#define TUNNEL_MODE_SEND_UDP 1
#define TUNNEL_MODE_REVERSE_TCP 2

#define TUNNEL_BUFFER_HIGH_WATERMARK 0x400000  // 4MB: stop reading
#define TUNNEL_BUFFER_LOW_WATERMARK  0x100000  // 1MB: resume reading
#define TUNNEL_BUFFER_HARD_CAP       0x1000000 // 16MB: close channel

struct TunnelData {
	ULONG  channelID;
	ULONG  type;
	SOCKET sock;
	BYTE   state;
	BYTE   mode;
	ULONG  i_address;
	WORD   port;
	ULONG  waitTime;
	ULONG  startTick;
	ULONG  closeTimer;
	CHAR*  writeBuffer;
	ULONG  writeBufferSize;
	BOOL   paused;
	BOOL   server_paused;
};

typedef struct _Proxyfire {
	Vector<TunnelData> tunnels;
} Proxyfire;

Proxyfire* Proxyfire_Create();
void Proxyfire_Destroy(Proxyfire* self);
void Proxyfire_ProcessTunnels(Proxyfire* self, Packer* packer);
void Proxyfire_CheckProxy(Proxyfire* self, Packer* packer);
ULONG Proxyfire_RecvProxy(Proxyfire* self, Packer* packer);
void Proxyfire_FlushProxy(Proxyfire* self, Packer* packer);
void Proxyfire_CloseProxy(Proxyfire* self);
void Proxyfire_ConnectMessageTCP(Proxyfire* self, ULONG channelId, ULONG type, CHAR* address, WORD port, Packer* outPacker);
void Proxyfire_ConnectMessageUDP(Proxyfire* self, ULONG channelId, CHAR* address, WORD port, Packer* outPacker);
void Proxyfire_ConnectWriteTCP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize, Packer* outPacker);
void Proxyfire_ConnectWriteUDP(Proxyfire* self, ULONG channelId, CHAR* data, ULONG dataSize);
void Proxyfire_ConnectClose(Proxyfire* self, ULONG channelId);
void Proxyfire_ConnectMessageReverse(Proxyfire* self, ULONG tunnelId, WORD port, Packer* outPacker);
void Proxyfire_ConnectPause(Proxyfire* self, ULONG channelId);
void Proxyfire_ConnectResume(Proxyfire* self, ULONG channelId);
void Proxyfire_AddProxyData(Proxyfire* self, ULONG channelId, ULONG type, SOCKET sock, ULONG waitTime, ULONG mode, ULONG address, WORD port, ULONG state);