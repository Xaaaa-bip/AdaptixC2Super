#include "MemorySaver.h"
#include <new>

MemorySaver* MemorySaver_Create()
{
	MemorySaver* self = (MemorySaver*)MemAllocLocal(sizeof(MemorySaver));
	new (&self->chunks) Map<ULONG, MemoryData>();
	return self;
}

void MemorySaver_Destroy(MemorySaver* self)
{
	if (!self)
		return;
	for (auto it = self->chunks.begin(); it != self->chunks.end(); ++it) {
		MemoryData& md = (*it).value;
		if (md.buffer)
			MemFreeLocal((LPVOID*)&md.buffer, md.totalSize);
	}
	self->chunks.destroy();
	self->chunks.~Map<ULONG, MemoryData>();
	MemFreeLocal((LPVOID*)&self, sizeof(MemorySaver));
}

void MemorySaver_WriteMemoryData(MemorySaver* self, ULONG memoryId, ULONG totalSize, ULONG dataSize, PBYTE data)
{
	if ( !self->chunks.contains(memoryId) ) {
		MemoryData memoryData = { 0 };
		memoryData.memoryId  = memoryId;
		memoryData.totalSize = totalSize;
		memoryData.buffer    = (PBYTE) MemAllocLocal(totalSize);
		
		self->chunks[memoryId] = memoryData;
	}

	memcpy( self->chunks[memoryId].buffer + self->chunks[memoryId].currentSize, data, dataSize );
	self->chunks[memoryId].currentSize += dataSize;

	if (self->chunks[memoryId].currentSize == self->chunks[memoryId].totalSize)
		self->chunks[memoryId].complete = true;
}

void MemorySaver_RemoveMemoryData(MemorySaver* self, ULONG memoryId)
{
	if (self->chunks[memoryId].buffer)
		MemFreeLocal((LPVOID*)(&self->chunks[memoryId].buffer), self->chunks[memoryId].totalSize);
	self->chunks[memoryId] = { 0 };
	self->chunks.remove(memoryId);
}