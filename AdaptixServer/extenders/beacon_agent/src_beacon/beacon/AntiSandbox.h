#pragma once
/*
 * Compile-time anti-sandbox detection header.
 *
 * Provides inline functions for:
 * 1. SleepEnforcement - checks that Sleep() actually sleeps
 * 2. DebugPortCheck - queries NtQueryInformationProcess
 * 3. NtGlobalFlagCheck - checks PEB flag anomalies
 * 4. TicksSinceBootCheck - verifies system uptime
 * 5. Synthetic timing delay to defeat dynamic analysis
 *
 * Usage:
 *   #include "AntiSandbox.h"
 *   if (AxAntiSandbox_Detect()) { fallback(); }
 *
 * Note: handle TRUE return as sandbox detected.
 */

#include "ApiLoader.h"
#include "ApiDefines.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 1. SleepEnforcement — verify Sleep actually blocks for ~50ms
 *    Many sandboxes skip/accelerate Sleep() calls.
 * ================================================================ */
static inline BOOL AxAntiSandbox_CheckSleepEnforcement(void)
{
    if (!ApiWin || !ApiWin->GetTickCount || !ApiWin->Sleep)
        return FALSE;

    ULONG t0 = ApiWin->GetTickCount();
    ApiWin->Sleep(120);
    ULONG elapsed = ApiWin->GetTickCount() - t0;

    /* If elapsed < 50ms, sleep was accelerated — sandbox indicator */
    return (elapsed < 50) ? TRUE : FALSE;
}

/* ================================================================
 * 2. DebugPortCheck — NtQueryInformationProcess ProcessDebugPort
 *    Non-zero value means a debugger is attached.
 * ================================================================ */
static inline BOOL AxAntiSandbox_CheckDebugPort(void)
{
    if (!ApiNt || !ApiNt->NtQueryInformationProcess)
        return FALSE;

    HANDLE hProcess = (HANDLE)(LONG_PTR)-1; /* NtCurrentProcess() */
    ULONG_PTR debugPort = 0;
    ULONG retLen = 0;
    NTSTATUS st = ApiNt->NtQueryInformationProcess(
        hProcess,
        (PROCESSINFOCLASS)7, /* ProcessDebugPort */
        &debugPort,
        sizeof(debugPort),
        &retLen
    );

    if (st >= 0 && debugPort != 0)
        return TRUE;

    return FALSE;
}

/* ================================================================
 * 3. PEB BeingDebugged + NtGlobalFlag check (from TEB)
 *    Classic debugger/sandbox indicator.
 * ================================================================ */
static inline BOOL AxAntiSandbox_CheckBeingDebugged(void)
{
    /* Read PEB via x64 inline asm (mov rax, gs:[0x60]) */
    PPEB peb = NULL;
#if defined(_M_X64) || defined(__x86_64__)
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(peb));
#elif defined(_M_IX86) || defined(__i386__)
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));
#else
    return FALSE;
#endif

    if (!peb)
        return FALSE;

    /* BeingDebugged flag (byte at offset 2 in PEB) */
    if (peb->BeingDebugged)
        return TRUE;

    /* NtGlobalFlag: 0x70 == FLG_HEAP_ENABLE_TAIL_CHECK | FLG_HEAP_ENABLE_FREE_CHECK | FLG_HEAP_VALIDATE_PARAMETERS */
    if (peb->NtGlobalFlag & 0x70)
        return TRUE;

    return FALSE;
}

/* ================================================================
 * 4. UptimeCheck — sandbox VMs often have very short uptime
 *    GetTickCount < 5 minutes suggests a freshly-launched sandbox.
 * ================================================================ */
static inline BOOL AxAntiSandbox_CheckUptime(void)
{
    if (!ApiWin || !ApiWin->GetTickCount)
        return FALSE;

    ULONG uptimeMs = ApiWin->GetTickCount();

    /* If uptime < 2 minutes (120000ms), highly suspicious */
    if (uptimeMs < 120000)
        return TRUE;

    return FALSE;
}

/* ================================================================
 * 5. Synthetic timing delay — defeats simple dynamic analysis
 * ================================================================ */
static inline void AxAntiSandbox_PerturbTiming(void)
{
    if (!ApiWin || !ApiWin->GetTickCount || !ApiWin->Sleep)
        return;

    ULONG tick = ApiWin->GetTickCount();
    ULONG delay = 30 + ((tick ^ (tick >> 8)) & 0x3F);
    if (delay > 90) delay = 90;
    ApiWin->Sleep(delay);
}

/* ================================================================
 * 6. Combined detection — returns TRUE if sandbox environment detected
 * ================================================================ */
static inline BOOL AxAntiSandbox_Detect(void)
{
#if defined(AX_ANTISANDBOX_DISABLE) && AX_ANTISANDBOX_DISABLE == 1
    return FALSE;
#endif

    /* Quick checks first — most reliable indicators */
    if (AxAntiSandbox_CheckBeingDebugged())
        return TRUE;

    if (AxAntiSandbox_CheckDebugPort())
        return TRUE;

    /* Timing-based checks (may trigger false positives) */
#if defined(AX_ANTISANDBOX_AGGRESSIVE) && AX_ANTISANDBOX_AGGRESSIVE == 1
    if (AxAntiSandbox_CheckUptime())
        return TRUE;

    if (AxAntiSandbox_CheckSleepEnforcement())
        return TRUE;
#endif

    return FALSE;
}

#ifdef __cplusplus
}
#endif
