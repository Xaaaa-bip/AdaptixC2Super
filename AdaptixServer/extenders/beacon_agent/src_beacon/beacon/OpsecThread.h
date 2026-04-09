#pragma once

#include <windows.h>

/* Yield through kernel32!SwitchToThread before long WinHTTP calls to bias stack sampling toward kernel32. */
void Opsec_PreBlockingWinHttp(void);

/* REFACTOR_STRATEGY §2.3: CREATE_SUSPENDED + ntdll!RtlUserThreadStart + SetThreadContext (x64). */
HANDLE Opsec_CreateAsyncBofThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, LPDWORD lpThreadId);
