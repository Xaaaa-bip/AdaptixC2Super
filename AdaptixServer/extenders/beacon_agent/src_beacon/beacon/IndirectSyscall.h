#pragma once

#include <windows.h>
#include "ntdll.h"

typedef struct _INDIRECT_NT_PROTECT_ARGS {
	HANDLE ProcessHandle;
	PVOID *BaseAddress;
	PSIZE_T RegionSize;
	ULONG NewProtect;
	ULONG Pad;
	PULONG OldProtect;
} INDIRECT_NT_PROTECT_ARGS;

typedef struct _INDIRECT_NT_ALLOC_ARGS {
	HANDLE ProcessHandle;
	PVOID *BaseAddress;
	ULONG_PTR ZeroBits;
	PSIZE_T RegionSize;
	ULONG AllocationType;
	ULONG Protect;
} INDIRECT_NT_ALLOC_ARGS;

typedef struct _INDIRECT_NT_CREATETHREADEX_ARGS {
	PHANDLE ThreadHandle;
	ULONG_PTR DesiredAccess;
	PVOID ObjectAttributes;
	ULONG_PTR ProcessHandle;
	PVOID StartRoutine;
	PVOID Argument;
	ULONG_PTR CreateFlags;
	ULONG_PTR ZeroBits;
	ULONG_PTR SizeOfStackCommit;
	ULONG_PTR SizeOfStackReserve;
	PVOID AttributeList;
} INDIRECT_NT_CREATETHREADEX_ARGS;

BOOL IndirectSyscall_Init(void);
BOOL IndirectSyscall_IsReady(void);
NTSTATUS Indirect_NtDelayExecution(BOOLEAN alertable, PLARGE_INTEGER interval);
NTSTATUS Indirect_NtProtectVirtualMemory(HANDLE processHandle, PVOID *baseAddress, PSIZE_T regionSize, ULONG newProtect, PULONG oldProtect);
NTSTATUS Indirect_NtAllocateVirtualMemory(HANDLE processHandle, PVOID *baseAddress, ULONG_PTR zeroBits, PSIZE_T regionSize, ULONG allocationType, ULONG protect);
LPVOID Indirect_VirtualAllocSensitive(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
NTSTATUS Indirect_NtCreateThreadEx(INDIRECT_NT_CREATETHREADEX_ARGS *args);
