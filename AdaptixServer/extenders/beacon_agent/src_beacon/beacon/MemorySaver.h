#pragma once

#include "std.cpp"
#include "Packer.h"

struct MemoryData {
	ULONG memoryId;
	ULONG totalSize;
	ULONG currentSize;
	PBYTE buffer;
	BOOL  complete;
};

typedef struct _MemorySaver {
	Map<ULONG, MemoryData> chunks;
} MemorySaver;

MemorySaver* MemorySaver_Create();
void MemorySaver_Destroy(MemorySaver* self);
void MemorySaver_WriteMemoryData(MemorySaver* self, ULONG memoryId, ULONG totalSize, ULONG dataSize, PBYTE data);
void MemorySaver_RemoveMemoryData(MemorySaver* self, ULONG memoryId);