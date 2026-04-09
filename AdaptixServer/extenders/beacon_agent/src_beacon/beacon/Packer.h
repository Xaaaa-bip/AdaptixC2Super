#pragma once
#include "ApiLoader.h"
#include "utils.h"

typedef struct _Packer {
	DWORD size;
	DWORD capacity;
	BYTE* buffer;
	DWORD index;
	BOOL  owns_buffer; /* FALSE: buffer is borrowed (e.g. connector recv); Destroy must not LocalFree it */
} Packer;

Packer* Packer_Create();
Packer* Packer_CreateWithBuffer(BYTE* buffer, ULONG size);
Packer* Packer_CreateView(BYTE* buffer, ULONG size);
void Packer_Destroy(Packer* self);

VOID Packer_Set32(Packer* self, ULONG index, ULONG value);
VOID Packer_EnsureCapacity(Packer* self, ULONG needed);

VOID Packer_Pack64(Packer* self, ULONG64 value);
VOID Packer_Pack32(Packer* self, ULONG value);
VOID Packer_Pack16(Packer* self, WORD value);
VOID Packer_Pack8(Packer* self, BYTE value);
VOID Packer_PackBytes(Packer* self, PBYTE data, ULONG data_size);
VOID Packer_PackFlatBytes(Packer* self, PBYTE data, ULONG data_size);
VOID Packer_PackStringA(Packer* self, LPSTR str);

BYTE  Packer_Unpack8(Packer* self);
ULONG Packer_Unpack32(Packer* self);
BYTE* Packer_UnpackBytes(Packer* self, ULONG* size);
BYTE* Packer_UnpackBytesCopy(Packer* self, ULONG* size);
BOOL  Packer_UnpackFlatBytes(Packer* self, PBYTE dest, ULONG len);

VOID  Packer_Clear(Packer* self, BOOL renew);
PBYTE Packer_data(Packer* self);
ULONG Packer_datasize(Packer* self);